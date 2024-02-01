/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/****************************************************************************
*
* This file is for bt_spp_vfs_acceptor demo. It can create servers, wait for connected and receive data.
* run bt_spp_vfs_acceptor demo, the bt_spp_vfs_initiator demo will automatically connect the bt_spp_vfs_acceptor demo,
* then receive data.
*
****************************************************************************/

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"
#include "spp_task.h"

#include "time.h"
#include "sys/time.h"

#include "esp_vfs.h"
#include "sys/unistd.h"

#include "sdkconfig.h"

#define SPP_TAG "SPP_ACCEPTOR_DEMO"
#define SPP_SERVER_NAME "SPP_SERVER"

#define BT_DEV_NAME_PREFIX CONFIG_DEV_NAME_PREFIX
#define BT_DEV_NAME_PREFIX_LEN (sizeof(BT_DEV_NAME_PREFIX) - 1)

#define BT_DEV_NAME_PREFIX_ALT CONFIG_DEV_NAME_PREFIX_ALT
#define BT_DEV_NAME_PREFIX_LEN_ALT (sizeof(BT_DEV_NAME_PREFIX_ALT) - 1)

#define BT_CONNECTED_GPIO  CONFIG_CONNECTED_LED_GPIO
#define BT_UART_TX_GPIO    CONFIG_UART_TX_GPIO
#define BT_UART_RX_GPIO    CONFIG_UART_RX_GPIO
#define BT_UART_RTS_GPIO   CONFIG_UART_RTS_GPIO

#define BT_LED_CONNECTED    0
#define BT_LED_DISCONNECTED 1

#define BT_UART_BITRATE     CONFIG_UART_BITRATE
#define BT_UART_BITRATE_ALT CONFIG_UART_BITRATE_ALT

#define BT_ALT_SWITCH_GPIO    CONFIG_ALT_SWITCH_GPIO
#define BT_ALT_INDICATOR_GPIO CONFIG_ALT_INDICATOR_GPIO

#ifdef CONFIG_UART_CTS_EN
#define BT_UART_FLOWCTRL   UART_HW_FLOWCTRL_CTS_RTS
#define BT_UART_CTS_GPIO   CONFIG_UART_CTS_GPIO
#else
#define BT_UART_FLOWCTRL   UART_HW_FLOWCTRL_RTS
#define BT_UART_CTS_GPIO   UART_PIN_NO_CHANGE
#endif

#define BT_UART_FLOWCTRL_ALT UART_HW_FLOWCTRL_DISABLE
#define BT_UART_PARITY_ALT   UART_PARITY_EVEN

#define BT_UART_RX_BUF_SZ (1024 * CONFIG_UART_RX_BUFF_SIZE)
#define BT_UART_TX_BUF_SZ (1024 * CONFIG_UART_TX_BUFF_SIZE)

static const esp_spp_mode_t esp_spp_mode = ESP_SPP_MODE_VFS;

static const esp_spp_sec_t sec_mask = ESP_SPP_SEC_AUTHENTICATE;
static const esp_spp_role_t role_slave = ESP_SPP_ROLE_SLAVE;

#define SPP_BUFF_SZ 128
static uint8_t spp_buff[SPP_BUFF_SZ];

static bool alt_settings;

static int uart_to_bt(int bt_fd, TickType_t ticks_to_wait)
{
    int size = uart_read_bytes(UART_NUM_1, spp_buff, SPP_BUFF_SZ, ticks_to_wait);
    if (size <= 0) {
        return 0;
    }
    ESP_LOGI(SPP_TAG, "UART -> %d bytes", size);
    uint8_t* ptr = spp_buff;
    int remain = size;
    while (remain > 0)
    {
        int res = write(bt_fd, ptr, remain);
        if (res < 0) {
            return -1;
        }
        if (res == 0) {
            vTaskDelay(1);
            continue;
        }
        ESP_LOGI(SPP_TAG, "BT <- %d bytes", res);
        remain -= res;
        ptr  += res;
    }
    return size;
}

#define SPP_BULK_RD_THRESHOLD 512

static void spp_read_handle(void * param)
{
    int fd = (int)param;

    ESP_LOGI(SPP_TAG, "BT connected");
    gpio_set_level(BT_CONNECTED_GPIO, BT_LED_CONNECTED);
    uart_flush(UART_NUM_1);

    for (;;)
    {
        size_t avail_now = 0;
        uart_get_buffered_data_len(UART_NUM_1, &avail_now);
        if (avail_now >= SPP_BULK_RD_THRESHOLD) {
            // Send available data from UART to BT first
            int remain = avail_now;
            while (remain >= SPP_BUFF_SZ) {
                int tx_size = uart_to_bt(fd, 0);
                if (tx_size < 0)
                    goto disconnected;
                if (!tx_size)
                    break;
                remain -= tx_size;
            }
        }
        // Try receive data from BT
        int size = read(fd, spp_buff, SPP_BUFF_SZ);
        if (size < 0) {
            goto disconnected;
        }
        if (size > 0) {
            ESP_LOGI(SPP_TAG, "BT -> %d bytes -> UART", size);
            uart_write_bytes(UART_NUM_1, (const char *)spp_buff, size);
            continue;
        }
        if (avail_now < SPP_BULK_RD_THRESHOLD) {
            // Read UART waiting several ticks for the new data
            if (uart_to_bt(fd, avail_now ? 1 : 2) < 0)
                goto disconnected;
        }
    }

disconnected:
    ESP_LOGI(SPP_TAG, "BT disconnected");
    gpio_set_level(BT_CONNECTED_GPIO, BT_LED_DISCONNECTED);
    spp_wr_task_shut_down();
}

static inline char hex_digit(uint8_t v)
{
    return v < 10 ? '0' + v : 'A' + v - 10;
}

static inline char byte_signature(uint8_t v)
{
    return hex_digit((v & 0xf) ^ (v >> 4));
}

#define BT_MAC_LEN 6

static void bt_set_device_name(void)
{
    char dev_name[BT_DEV_NAME_PREFIX_LEN + BT_MAC_LEN + 1] = BT_DEV_NAME_PREFIX;
    const uint8_t * mac = esp_bt_dev_get_address();
    int i;
    for (i = 0; i < BT_MAC_LEN; ++i) {
        dev_name[BT_DEV_NAME_PREFIX_LEN + i] = byte_signature(mac[i]);
    }
    dev_name[BT_DEV_NAME_PREFIX_LEN + BT_MAC_LEN] = 0;
    ESP_ERROR_CHECK(esp_bt_dev_set_device_name(dev_name));
    ESP_LOGI(SPP_TAG, "Device name is %s", dev_name);
}

static void bt_set_alt_device_name(void)
{
    char dev_name[BT_DEV_NAME_PREFIX_LEN_ALT + BT_MAC_LEN + 1] = BT_DEV_NAME_PREFIX_ALT;
    const uint8_t * mac = esp_bt_dev_get_address();
    int i;
    for (i = 0; i < BT_MAC_LEN; ++i) {
        dev_name[BT_DEV_NAME_PREFIX_LEN_ALT + i] = byte_signature(mac[i]);
    }
    dev_name[BT_DEV_NAME_PREFIX_LEN_ALT + BT_MAC_LEN] = 0;
    ESP_ERROR_CHECK(esp_bt_dev_set_device_name(dev_name));
    ESP_LOGI(SPP_TAG, "Device name (alt) is %s", dev_name);
}

static void esp_spp_cb(uint16_t e, void *p)
{
    esp_spp_cb_event_t event = e;
    esp_spp_cb_param_t *param = p;

    switch (event) {
    case ESP_SPP_INIT_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_INIT_EVT");
		if (alt_settings)
			bt_set_alt_device_name();
		else
			bt_set_device_name();
        esp_bt_gap_set_scan_mode(ESP_BT_SCAN_MODE_CONNECTABLE_DISCOVERABLE);
        esp_spp_start_srv(sec_mask,role_slave, 0, SPP_SERVER_NAME);
        break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_DISCOVERY_COMP_EVT");
        break;
    case ESP_SPP_OPEN_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_OPEN_EVT");
        break;
    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_CLOSE_EVT");
        break;
    case ESP_SPP_START_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_START_EVT");
        break;
    case ESP_SPP_CL_INIT_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_CL_INIT_EVT");
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_SRV_OPEN_EVT");
        spp_wr_task_start_up(spp_read_handle, param->srv_open.fd);
        break;
    default:
        break;
    }
}

static void esp_spp_stack_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    spp_task_work_dispatch(esp_spp_cb, event, param, sizeof(esp_spp_cb_param_t), NULL);
}

void esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:{
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(SPP_TAG, "authentication success: %s", param->auth_cmpl.device_name);
            esp_log_buffer_hex(SPP_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
        } else {
            ESP_LOGE(SPP_TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
        }
        break;
    }
    case ESP_BT_GAP_PIN_REQ_EVT:{
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit) {
            ESP_LOGI(SPP_TAG, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
            ESP_LOGI(SPP_TAG, "Input pin code: 1234");
            esp_bt_pin_code_t pin_code;
            pin_code[0] = '1';
            pin_code[1] = '2';
            pin_code[2] = '3';
            pin_code[3] = '4';
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
    }
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %d", param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%d", param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
    default: {
        ESP_LOGI(SPP_TAG, "event: %d", event);
        break;
    }
    }
    return;
}

void app_main()
{
    /* Configure GPIO mux */
    gpio_pad_select_gpio(BT_ALT_SWITCH_GPIO);
    gpio_set_direction(BT_ALT_SWITCH_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BT_ALT_SWITCH_GPIO, GPIO_PULLUP_ONLY);

    gpio_pad_select_gpio(BT_CONNECTED_GPIO);
    gpio_set_direction(BT_CONNECTED_GPIO, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(BT_CONNECTED_GPIO, BT_LED_DISCONNECTED);

    alt_settings = !gpio_get_level(BT_ALT_SWITCH_GPIO);
    if (alt_settings) {
        gpio_pad_select_gpio(BT_ALT_INDICATOR_GPIO);
        gpio_set_level(BT_ALT_INDICATOR_GPIO, 1);
        gpio_set_direction(BT_ALT_INDICATOR_GPIO, GPIO_MODE_OUTPUT);
    }

    /* Configure UART */
    uart_config_t uart_config = {
        .baud_rate = alt_settings ? BT_UART_BITRATE_ALT : BT_UART_BITRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = alt_settings ? BT_UART_PARITY_ALT : UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = alt_settings ? BT_UART_FLOWCTRL_ALT : BT_UART_FLOWCTRL,
        .rx_flow_ctrl_thresh = UART_FIFO_LEN - 4
    };

    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, BT_UART_TX_GPIO, BT_UART_RX_GPIO, BT_UART_RTS_GPIO, BT_UART_CTS_GPIO));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, BT_UART_RX_BUF_SZ, BT_UART_TX_BUF_SZ, 0, NULL, 0));

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if (esp_bt_controller_init(&bt_cfg) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s initialize controller failed", __func__);
        return;
    }

    if (esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s enable controller failed", __func__);
        return;
    }

    if (esp_bluedroid_init() != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s initialize bluedroid failed", __func__);
        return;
    }

    if (esp_bluedroid_enable() != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s enable bluedroid failed", __func__);
        return;
    }

    if (esp_bt_gap_register_callback(esp_bt_gap_cb) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s gap register failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if (esp_spp_register_callback(esp_spp_stack_cb) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s spp register failed", __func__);
        return;
    }
    esp_spp_vfs_register();
    spp_task_task_start_up();

    if (esp_spp_init(esp_spp_mode) != ESP_OK) {
        ESP_LOGE(SPP_TAG, "%s spp init failed", __func__);
        return;
    }

    /* Set default parameters for Secure Simple Pairing */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    /*
     * Set default parameters for Legacy Pairing
     * Use variable pin, input pin code when pairing
     */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);
}

