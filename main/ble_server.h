#pragma once

#include "sdkconfig.h"

#ifdef CONFIG_BTDM_CONTROLLER_MODE_BTDM
#define BLE_ADAPTER_EN
#endif

#ifdef BLE_ADAPTER_EN

void ble_server_init(void);

#endif
