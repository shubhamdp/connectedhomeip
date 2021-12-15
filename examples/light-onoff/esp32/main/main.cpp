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

#include "BluetoothWidget.h"
#include "Button.h"
#include "CHIPDeviceManager.h"
#include "DeviceCallbacks.h"
#include "Display.h"
#include "Globals.h"
#include "LEDWidget.h"
#include "ListScreen.h"
#include "OpenThreadLaunch.h"
#include "QRCodeScreen.h"
#include "ScreenManager.h"
#include "WiFiWidget.h"
#include "esp_heap_caps_init.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_spi_flash.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "platform/PlatformManager.h"
#include "shell_extension/launch.h"

#include <cmath>
#include <cstdio>
#include <ctype.h>
#include <string>
#include <vector>

#include <app-common/zap-generated/att-storage.h>
#include <app-common/zap-generated/attribute-id.h>
#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-id.h>
#include <app/server/AppDelegate.h>
#include <app/server/Dnssd.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <app/util/af.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/shell/Engine.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/ErrorStr.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/ManualSetupPayloadGenerator.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <zap-generated/af-gen-event.h>

#include <app/clusters/on-off-server/on-off-server.h>

#if CONFIG_ENABLE_PW_RPC
#include "Rpc.h"
#endif

#if CONFIG_OPENTHREAD_ENABLED
#include <platform/ThreadStackManager.h>
#endif

using namespace ::chip;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceManager;
using namespace ::chip::DeviceLayer;

#if CONFIG_DEVICE_TYPE_M5STACK

#define BUTTON_1_GPIO_NUM GPIO_NUM_39    // Left button on M5Stack
#define BUTTON_2_GPIO_NUM GPIO_NUM_38    // Middle button on M5Stack
#define BUTTON_3_GPIO_NUM GPIO_NUM_37    // Right button on M5Stack
#define STATUS_LED_GPIO_NUM GPIO_NUM_MAX // No status LED on M5Stack

#elif CONFIG_DEVICE_TYPE_ESP32_WROVER_KIT

#define STATUS_LED_GPIO_NUM GPIO_NUM_26

#elif CONFIG_DEVICE_TYPE_ESP32_DEVKITC

#define STATUS_LED_GPIO_NUM GPIO_NUM_2 // Use LED1 (blue LED) as status LED on DevKitC

#elif CONFIG_DEVICE_TYPE_ESP32_C3_DEVKITM

#define STATUS_LED_GPIO_NUM GPIO_NUM_8

#else // !CONFIG_DEVICE_TYPE_ESP32_DEVKITC

#error "Unsupported device type selected"

#endif // !CONFIG_DEVICE_TYPE_ESP32_DEVKITC

// Used to indicate that an IP address has been added to the QRCode
#define EXAMPLE_VENDOR_TAG_IP 1

const char * TAG = "all-clusters-app";

static DeviceCallbacks EchoCallbacks;

namespace {

#if CONFIG_DEVICE_TYPE_M5STACK

std::vector<Button> buttons          = { Button(), Button(), Button() };
std::vector<gpio_num_t> button_gpios = { BUTTON_1_GPIO_NUM, BUTTON_2_GPIO_NUM, BUTTON_3_GPIO_NUM };

#endif

// Pretend these are devices with endpoints with clusters with attributes
typedef std::tuple<std::string, std::string> Attribute;
typedef std::vector<Attribute> Attributes;
typedef std::tuple<std::string, Attributes> Cluster;
typedef std::vector<Cluster> Clusters;
typedef std::tuple<std::string, Clusters> Endpoint;
typedef std::vector<Endpoint> Endpoints;
typedef std::tuple<std::string, Endpoints> Device;
typedef std::vector<Device> Devices;
Devices devices;

void AddAttribute(std::string name, std::string value)
{
    Attribute attribute = std::make_tuple(std::move(name), std::move(value));
    std::get<1>(std::get<1>(std::get<1>(devices.back()).back()).back()).emplace_back(std::move(attribute));
}

void AddCluster(std::string name)
{
    Cluster cluster = std::make_tuple(std::move(name), std::move(Attributes()));
    std::get<1>(std::get<1>(devices.back()).back()).emplace_back(std::move(cluster));
}

void AddEndpoint(std::string name)
{
    Endpoint endpoint = std::make_tuple(std::move(name), std::move(Clusters()));
    std::get<1>(devices.back()).emplace_back(std::move(endpoint));
}

void AddDevice(std::string name)
{
    Device device = std::make_tuple(std::move(name), std::move(Endpoints()));
    devices.emplace_back(std::move(device));
}

void SetupPretendDevices()
{
    AddDevice("Light Bulb");
    AddEndpoint("1");
    AddCluster("OnOff");
    AddAttribute("OnOff", "Off");
}

WiFiWidget pairingWindowLED;

class AppCallbacks : public AppDelegate
{
public:
    void OnRendezvousStarted() override { bluetoothLED.Set(true); }
    void OnRendezvousStopped() override
    {
        bluetoothLED.Set(false);
        pairingWindowLED.Set(false);
    }
    void OnPairingWindowOpened() override { pairingWindowLED.Set(true); }
    void OnPairingWindowClosed() override { pairingWindowLED.Set(false); }
};

AppCallbacks sCallbacks;

} // namespace

static void InitServer(intptr_t context)
{
    // Init ZCL Data Model and CHIP App Server
    chip::Server::GetInstance().Init(&sCallbacks);

    // Initialize device attestation config
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());

    SetupPretendDevices();
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "Light On Off Demo!");

    // Initialize the ESP NVS layer.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init() failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGE(TAG, "Free:%d MinFree:%d Lfb:%d", heap_caps_get_free_size(MALLOC_CAP_8BIT),
             heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#if CONFIG_ENABLE_PW_RPC
    chip::rpc::Init();
#endif

#if CONFIG_ENABLE_CHIP_SHELL
    chip::LaunchShell();
#endif // CONFIG_ENABLE_CHIP_SHELL

#if CONFIG_OPENTHREAD_ENABLED
    LaunchOpenThread();
    ThreadStackMgr().InitThreadStack();
#endif

    CHIPDeviceManager & deviceMgr = CHIPDeviceManager::GetInstance();

    CHIP_ERROR error = deviceMgr.Init(&EchoCallbacks);
    if (error != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "device.Init() failed: %s", ErrorStr(error));
        return;
    }

    statusLED1.Init(STATUS_LED_GPIO_NUM);
    // Our second LED doesn't map to any physical LEDs so far, just to virtual
    // "LED"s on devices with screens.
    statusLED2.Init(GPIO_NUM_MAX);
    bluetoothLED.Init();
    wifiLED.Init();
    pairingWindowLED.Init();

    chip::DeviceLayer::PlatformMgr().ScheduleWork(InitServer, reinterpret_cast<intptr_t>(nullptr));

    // Print QR Code URL
    PrintOnboardingCodes(chip::RendezvousInformationFlags(CONFIG_RENDEZVOUS_MODE));

#if CONFIG_HAVE_DISPLAY
    std::string qrCodeText;

    GetQRCode(qrCodeText, chip::RendezvousInformationFlags(CONFIG_RENDEZVOUS_MODE));

    // Initialize the display device.
    err = InitDisplay();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "InitDisplay() failed: %s", esp_err_to_name(err));
        return;
    }

    // Initialize the screen manager
    ScreenManager::Init();

#if CONFIG_DEVICE_TYPE_M5STACK
    // Initialize the buttons.
    for (int i = 0; i < buttons.size(); ++i)
    {
        err = buttons[i].Init(button_gpios[i], 50);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Button.Init() failed: %s", esp_err_to_name(err));
            return;
        }
    }

    // Push a rudimentary user interface.
    ScreenManager::PushScreen(chip::Platform::New<ListScreen>(
        (chip::Platform::New<SimpleListModel>())
            ->Title("CHIP")
            ->Action([](int i) { ESP_LOGI(TAG, "action on item %d", i); })
            ->Item("Devices",
                   []() {
                       ESP_LOGI(TAG, "Opening device list");
                       ScreenManager::PushScreen(chip::Platform::New<ListScreen>(chip::Platform::New<DeviceListModel>()));
                   })
            ->Item("mDNS Debug",
                   []() {
                       ESP_LOGI(TAG, "Opening MDNS debug");
                       ScreenManager::PushScreen(chip::Platform::New<ListScreen>(chip::Platform::New<MdnsDebugListModel>()));
                   })
            ->Item("QR Code",
                   [=]() {
                       ESP_LOGI(TAG, "Opening QR code screen");
                       ESP_LOGI(TAG, "QR CODE Text: '%s'", qrCodeText.c_str());
                       uint16_t discriminator;
                       if (ConfigurationMgr().GetSetupDiscriminator(discriminator) == CHIP_NO_ERROR)
                       {
                           ESP_LOGI(TAG, "Setup discriminator: %u (0x%x)", discriminator, discriminator);
                       }
                       uint32_t setupPINCode;
                       if (ConfigurationMgr().GetSetupPinCode(setupPINCode) == CHIP_NO_ERROR)
                       {
                           ESP_LOGI(TAG, "Setup PIN code: %u (0x%x)", setupPINCode, setupPINCode);
                       }
                       ScreenManager::PushScreen(chip::Platform::New<QRCodeScreen>(qrCodeText));
                   })
            ->Item("Setup",
                   [=]() {
                       ESP_LOGI(TAG, "Opening Setup list");
                       ScreenManager::PushScreen(chip::Platform::New<ListScreen>(chip::Platform::New<SetupListModel>()));
                   })
            ->Item("Custom",
                   []() {
                       ESP_LOGI(TAG, "Opening custom screen");
                       ScreenManager::PushScreen(chip::Platform::New<CustomScreen>());
                   })
            ->Item("More")
            ->Item("Items")
            ->Item("For")
            ->Item("Demo")));

#elif CONFIG_DEVICE_TYPE_ESP32_WROVER_KIT

    // Display the QR Code
    QRCodeScreen qrCodeScreen(qrCodeText);
    qrCodeScreen.Display();

#endif

    // Connect the status LED to VLEDs.
    {
        int vled1 = ScreenManager::AddVLED(TFT_GREEN);
        int vled2 = ScreenManager::AddVLED(TFT_RED);
        statusLED1.SetVLED(vled1, vled2);

        int vled3 = ScreenManager::AddVLED(TFT_CYAN);
        int vled4 = ScreenManager::AddVLED(TFT_ORANGE);
        statusLED2.SetVLED(vled3, vled4);

        bluetoothLED.SetVLED(ScreenManager::AddVLED(TFT_BLUE));
        wifiLED.SetVLED(ScreenManager::AddVLED(TFT_YELLOW));
        pairingWindowLED.SetVLED(ScreenManager::AddVLED(TFT_ORANGE));
    }

#endif // CONFIG_HAVE_DISPLAY

#if CONFIG_DEVICE_TYPE_M5STACK
    // Run the UI Loop
    while (true)
    {
        // TODO consider refactoring this example to use FreeRTOS tasks

        bool woken = false;

        // Poll buttons, possibly wake screen.
        for (int i = 0; i < buttons.size(); ++i)
        {
            if (buttons[i].Poll())
            {
                if (!woken)
                {
                    woken = WakeDisplay();
                }
                if (woken)
                {
                    continue;
                }
                if (buttons[i].IsPressed())
                {
                    ScreenManager::ButtonPressed(1 + i);
                }
            }
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
#endif // CONFIG_DEVICE_TYPE_M5STACK
    ESP_LOGE(TAG, "Free:%d MinFree:%d Lfb:%d", heap_caps_get_free_size(MALLOC_CAP_8BIT),
             heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

bool lowPowerClusterSleep()
{
    return true;
}
