/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <esp_log.h>
#include <esp_spi_flash.h>
#include <nvs_flash.h>
#include <lib/support/ErrorStr.h>
// #include <app_wifi.h>
#include <esp_heap_caps.h>
#include <ESP32Controller.h>

using namespace ::chip;
using namespace ::chip::DeviceLayer;

const char * TAG = "chip-tool";

static void alloc_fail_cb(size_t size, uint32_t caps, const char * function_name)
{
    ets_printf("Allocation failed in %s: %d bytes, caps: %d\n", function_name, size, caps);
    ets_printf("free: %u lfb:%u\n", heap_caps_get_free_size(caps),
                                    heap_caps_get_largest_free_block(caps));
}

__attribute__((constructor)) void set_alloc_fail_hook(void)
{
    heap_caps_register_failed_alloc_callback(alloc_fail_cb);
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "CHIP Controller!");

    // Initialize the ESP NVS layer.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init() failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "nvs_flash_init() succeeded");

//    app_wifi_sta_init("maverick", "qwertyuiop");

#if CONFIG_ENABLE_CHIP_SHELL
    // chip::LaunchShell();
#endif // CONFIG_ENABLE_CHIP_SHELL

    ESP32Controller & controller = ESP32Controller::GetInstance();
    controller.Init();
    chip::Controller::DeviceCommissioner & commissioner = controller.GetCommissioner();

    DevicePairingCommands & pairingCommands = DevicePairingCommands::GetInstance();

    commissioner.RegisterPairingDelegate(&pairingCommands);
    
    pairingCommands.SetDeviceCommissioner(&commissioner);
    pairingCommands.PairBleWifi(134, 20202021, 3840, "maverick", "qwertyuiop");
}
