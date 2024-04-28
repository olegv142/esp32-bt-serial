/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/


#include "ble_server.h"

#ifdef BLE_ADAPTER_EN

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "driver/uart.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GATTS_TABLE_TAG  "GATTS_SPP"

// Attributes State Machine
enum{
    SPP_IDX_SVC,
    SPP_IDX_SPP_DATA_NOTIFY_CHAR,
    SPP_IDX_SPP_DATA_NTY_VAL,
    SPP_IDX_SPP_DATA_NTF_CFG,

    SPP_IDX_NB,
};

#define SPP_PROFILE_NUM             1
#define SPP_PROFILE_APP_IDX         0
#define ESP_SPP_APP_ID              0x56
#define SPP_SVC_INST_ID	            0
#define SPP_DATA_MAX_LEN           (512)

// SPP Service
static const uint16_t spp_service_uuid = 0xFFE0;
// Characteristic UUID
#define ESP_GATT_UUID_SPP_DATA_NOTIFY       0xFFE1

#define BLE_ADV_NAME      CONFIG_DEV_NAME_BLE
#define BLE_ADV_NAME_LEN (sizeof(BLE_ADV_NAME)-1)
#define BLE_ADV_NAME_OFF  15

static uint8_t spp_adv_data[BLE_ADV_NAME_OFF+BLE_ADV_NAME_LEN+DEV_NAME_SUFF_LEN] = {
    0x02, 0x01, 0x04,                   // flags
    0x03, 0x03, 0xe0, 0xff,             // service UID
    0x05, 0x12, 0x20, 0x00, 0x40, 0x00, // conn interval range
    1+BLE_ADV_NAME_LEN+DEV_NAME_SUFF_LEN, 0x09,
    // [BLE_ADV_NAME_OFF..] BLE_ADV_NAME + suffix
};

#define BLE_UART_NUM    UART_NUM_2
#ifdef CONFIG_BLE_UART_PARITY
#define BLE_UART_PARITY UART_PARITY_EVEN
#else
#define BLE_UART_PARITY UART_PARITY_DISABLE
#endif

static uint16_t spp_mtu_size = 23;
static uint8_t  spp_seq = 0;
static uint8_t  spp_seq_max = 15;
static uint16_t spp_conn_id = 0xffff;
static esp_gatt_if_t spp_gatts_if = 0xff;
QueueHandle_t spp_uart_queue = NULL;

static bool enable_data_ntf = false;
static bool is_connected = false;
static esp_bd_addr_t spp_remote_bda = {0x0,};

static uint16_t spp_handle_table[SPP_IDX_NB];

static esp_ble_adv_params_t spp_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/* One gatt-based profile one app_id and one gatts_if, this array will store the gatts_if returned by ESP_GATTS_REG_EVT */
static struct gatts_profile_inst spp_profile_tab[SPP_PROFILE_NUM] = {
    [SPP_PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

/*
 *  SPP PROFILE ATTRIBUTES
 ****************************************************************************************
 */

#define CHAR_DECLARATION_SIZE   (sizeof(uint8_t))
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ|ESP_GATT_CHAR_PROP_BIT_NOTIFY;

// SPP Service - data notify characteristic, notify&read
static const uint16_t spp_data_notify_uuid = ESP_GATT_UUID_SPP_DATA_NOTIFY;
static const uint8_t  spp_data_notify_val[20] = {0x00};
static const uint8_t  spp_data_notify_ccc[2] = {0x00, 0x00};

// Full HRS Database Description - Used to add attributes into the database
static const esp_gatts_attr_db_t spp_gatt_db[SPP_IDX_NB] =
{
    //SPP -  Service Declaration
    [SPP_IDX_SVC]                      	=
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ,
    sizeof(spp_service_uuid), sizeof(spp_service_uuid), (uint8_t *)&spp_service_uuid}},

    //SPP -  data notify characteristic Declaration
    [SPP_IDX_SPP_DATA_NOTIFY_CHAR]  =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ,
    CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_notify}},

    //SPP -  data notify characteristic Value
    [SPP_IDX_SPP_DATA_NTY_VAL]   =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&spp_data_notify_uuid, ESP_GATT_PERM_READ,
    SPP_DATA_MAX_LEN, sizeof(spp_data_notify_val), (uint8_t *)spp_data_notify_val}},

    //SPP -  data notify characteristic - Client Characteristic Configuration Descriptor
    [SPP_IDX_SPP_DATA_NTF_CFG]         =
    {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ|ESP_GATT_PERM_WRITE,
    sizeof(uint16_t),sizeof(spp_data_notify_ccc), (uint8_t *)spp_data_notify_ccc}},
};

static uint8_t find_char_and_desr_index(uint16_t handle)
{
    uint8_t error = 0xff;

    for(int i = 0; i < SPP_IDX_NB ; i++){
        if(handle == spp_handle_table[i]){
            return i;
        }
    }

    return error;
}

void uart_task(void *pvParameters)
{
    for (;;) {
        // Waiting for UART event.
        uart_event_t event;
        if (xQueueReceive(spp_uart_queue, (void*)&event, (portTickType)portMAX_DELAY)) {
            switch (event.type) {
            //Event of UART receving data
            case UART_DATA:
                if (event.size) {
                    uint8_t * const buff = (uint8_t*)malloc(1 + event.size);
                    if (buff == NULL) {
                        ESP_LOGE(GATTS_TABLE_TAG, "%s malloc.1 failed", __func__);
                        break;
                    }
                    uart_read_bytes(BLE_UART_NUM, buff + 1, event.size, portMAX_DELAY);
                    if (!is_connected) {
                        ESP_LOGW(GATTS_TABLE_TAG, "%s not connected", __func__);
                    } else if (!enable_data_ntf) {
                        ESP_LOGW(GATTS_TABLE_TAG, "%s notify not enabled", __func__);
                    } else {
                        uint16_t const max_payload = spp_mtu_size - 3;
                        uint16_t const max_chunk = max_payload - 1;
                        int const nchunks = (event.size + max_chunk - 1) / max_chunk;
                        int remain = event.size;
                        ESP_LOGD(GATTS_TABLE_TAG, "%s %u / %u", __func__, event.size, max_chunk);
                        for (int i = 0; i < nchunks; ++i) {
                            int const chunk = remain <= max_chunk ? remain : max_chunk;
                            uint8_t first_tag = 'a';
                            buff[i*max_chunk] = first_tag + spp_seq;
                            if (++spp_seq > spp_seq_max)
                                spp_seq = 0;
                            esp_ble_gatts_send_indicate(spp_gatts_if, spp_conn_id, spp_handle_table[SPP_IDX_SPP_DATA_NTY_VAL], 1 + chunk, &buff[i*max_chunk], false);
                        }
                    }
                    free(buff);
                }
                break;
            default:
                break;
            }
        }
    }
    vTaskDelete(NULL);
}

static void spp_uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = CONFIG_BLE_UART_BITRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = BLE_UART_PARITY,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = UART_FIFO_LEN - 4,
    };

    // Set UART parameters
    ESP_ERROR_CHECK(uart_param_config(BLE_UART_NUM, &uart_config));
    // Set UART pins
    ESP_ERROR_CHECK(uart_set_pin(BLE_UART_NUM, UART_PIN_NO_CHANGE, CONFIG_BLE_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // Install UART driver, and get the queue.
    ESP_ERROR_CHECK(uart_driver_install(BLE_UART_NUM, 4096, 8192, 10, &spp_uart_queue, 0));
    xTaskCreate(uart_task, "uTask", 2048, (void*)BLE_UART_NUM, 8, NULL);
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    esp_err_t err;
    ESP_LOGI(GATTS_TABLE_TAG, "GAP_EVT, event %d", event);

    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&spp_adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        // advertising start complete event to indicate advertising start successfully or failed
        if((err = param->adv_start_cmpl.status) != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(GATTS_TABLE_TAG, "Advertising start failed: %s", esp_err_to_name(err));
        }
        break;
    default:
        break;
    }
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    esp_ble_gatts_cb_param_t *p_data = (esp_ble_gatts_cb_param_t *) param;
    uint8_t res = 0xff;

    ESP_LOGD(GATTS_TABLE_TAG, "event = %x", event);
    switch (event) {
    	case ESP_GATTS_REG_EVT:
            ESP_LOGI(GATTS_TABLE_TAG, "%s %d", __func__, __LINE__);
            esp_ble_gap_set_device_name(get_device_name());
            esp_ble_gap_config_adv_data_raw((uint8_t *)spp_adv_data, sizeof(spp_adv_data));
            esp_ble_gatts_create_attr_tab(spp_gatt_db, gatts_if, SPP_IDX_NB, SPP_SVC_INST_ID);
            break;
    	case ESP_GATTS_READ_EVT:
            break;
        case ESP_GATTS_WRITE_EVT:
    	    res = find_char_and_desr_index(p_data->write.handle);
            if (p_data->write.is_prep == false){
                ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_WRITE_EVT : handle = %d", res);
                if (res == SPP_IDX_SPP_DATA_NTF_CFG) {
                    if(p_data->write.len == 2 && p_data->write.value[1] == 0x00) {
                        if (p_data->write.value[0] == 0x01) {
                            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_WRITE_EVT : notification enabled");
                            enable_data_ntf = true;
                        } else if (p_data->write.value[0] == 0x00) {
                            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_WRITE_EVT : notification disabled");
                            enable_data_ntf = false;
                        }
                    }
                }
            }
      	 	break;
        case ESP_GATTS_EXEC_WRITE_EVT:
            break;
    	case ESP_GATTS_MTU_EVT:
    	    spp_mtu_size = p_data->mtu.mtu;
            ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_MTU_EVT mtu = %d", spp_mtu_size);
    	    break;
    	case ESP_GATTS_CONF_EVT:
    	case ESP_GATTS_UNREG_EVT:
    	case ESP_GATTS_DELETE_EVT:
    	case ESP_GATTS_START_EVT:
    	case ESP_GATTS_STOP_EVT:
        	break;
    	case ESP_GATTS_CONNECT_EVT:
    	    spp_conn_id = p_data->connect.conn_id;
    	    spp_gatts_if = gatts_if;
    	    is_connected = true;
    	    memcpy(&spp_remote_bda,&p_data->connect.remote_bda,sizeof(esp_bd_addr_t));
        	break;
    	case ESP_GATTS_DISCONNECT_EVT:
    	    is_connected = false;
    	    enable_data_ntf = false;
    	    esp_ble_gap_start_advertising(&spp_adv_params);
    	    break;
    	case ESP_GATTS_OPEN_EVT:
    	case ESP_GATTS_CANCEL_OPEN_EVT:
    	case ESP_GATTS_CLOSE_EVT:
    	case ESP_GATTS_LISTEN_EVT:
    	case ESP_GATTS_CONGEST_EVT:
    	    break;
    	case ESP_GATTS_CREAT_ATTR_TAB_EVT:{
            ESP_LOGI(GATTS_TABLE_TAG, "The number handle =%x",param->add_attr_tab.num_handle);
    	    if (param->add_attr_tab.status != ESP_GATT_OK){
    	        ESP_LOGE(GATTS_TABLE_TAG, "Create attribute table failed, error code=0x%x", param->add_attr_tab.status);
    	    }
    	    else if (param->add_attr_tab.num_handle != SPP_IDX_NB){
    	        ESP_LOGE(GATTS_TABLE_TAG, "Create attribute table abnormally, num_handle (%d) doesn't equal to HRS_IDX_NB(%d)", param->add_attr_tab.num_handle, SPP_IDX_NB);
    	    }
    	    else {
    	        memcpy(spp_handle_table, param->add_attr_tab.handles, sizeof(spp_handle_table));
    	        esp_ble_gatts_start_service(spp_handle_table[SPP_IDX_SVC]);
    	    }
    	    break;
    	}
    	default:
    	    break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGD(GATTS_TABLE_TAG, "EVT %d, gatts if %d", event, gatts_if);

    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            spp_profile_tab[SPP_PROFILE_APP_IDX].gatts_if = gatts_if;
        } else {
            ESP_LOGI(GATTS_TABLE_TAG, "Reg app failed, app_id %04x, status %d",param->reg.app_id, param->reg.status);
            return;
        }
    }

    do {
        int idx;
        for (idx = 0; idx < SPP_PROFILE_NUM; idx++) {
            if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gatts_if == spp_profile_tab[idx].gatts_if) {
                if (spp_profile_tab[idx].gatts_cb) {
                    spp_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

void ble_server_init(void)
{
    for (int i = 0; i < BLE_ADV_NAME_LEN; ++i)
        spp_adv_data[BLE_ADV_NAME_OFF+i] = BLE_ADV_NAME[i];
    get_device_name_suff((char*)&spp_adv_data[BLE_ADV_NAME_OFF+BLE_ADV_NAME_LEN]);

    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gap_register_callback(gap_event_handler);
    esp_ble_gatts_app_register(ESP_SPP_APP_ID);

    spp_uart_init();
}

#endif
