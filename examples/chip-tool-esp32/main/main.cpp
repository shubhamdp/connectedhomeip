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

#include <CHIPDeviceManager.h>
#include <esp_log.h>
#include <esp_spi_flash.h>
#include <nvs_flash.h>
#include <app/server/Server.h>
#include <lib/support/ErrorStr.h>
#include <shell_extension/launch.h>
#include <lib/shell/Engine.h>
#include <app_wifi.h>

#include <ESP32Controller.h>

using namespace ::chip;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceManager;
using namespace ::chip::DeviceLayer;

const char * TAG = "chip-tool";

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
    app_wifi_sta_init("maverick", "qwertyuiop");

 //   CHIPDeviceManager & deviceMgr = CHIPDeviceManager::GetInstance();

 //    CHIP_ERROR error = deviceMgr.Init(nullptr);
 //    if (error != CHIP_NO_ERROR)
 //    {
 //        ESP_LOGE(TAG, "device.Init() failed: %s", ErrorStr(error));
 //        return;
 //    }

//    chip::Server::GetInstance().Init();

#if CONFIG_ENABLE_CHIP_SHELL
    // chip::LaunchShell();
#endif // CONFIG_ENABLE_CHIP_SHELL

    ESP32Controller & controller = ESP32Controller::GetInstance();
    commissioner.Init();
    chip::Controller::CHIPDeviceCommissioner & commissioner = commissioner.GetCommissioner();

    DevicePairingCommands & pairingCommands = DevicePairingCommands::GetInstance();

    commissioner.RegisterPairingDelegate(&pairingCommands);
    
    pairingCommands.SetDeviceCommissioner(&commissioner);
    pairingCommands.PairBleWifi(134, 20202021, 3840, "maverick", "qwertyuiop");
}
