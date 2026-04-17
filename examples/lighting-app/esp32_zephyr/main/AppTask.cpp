/*
 *    Copyright (c) 2024 Project CHIP Authors
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

#include "AppTask.h"

#include "AppEvent.h"
#include "LEDWidget.h"

#include <DeviceInfoProviderImpl.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/TestEventTriggerDelegate.h>
#include <app/clusters/identify-server/identify-server.h>
#include <app/clusters/ota-requestor/OTATestEventTriggerHandler.h>
#include <app/persistence/AttributePersistenceProviderInstance.h>
#include <app/persistence/DefaultAttributePersistenceProvider.h>
#include <app/persistence/DeferredAttributePersistenceProvider.h>
#include <app/server/Dnssd.h>
#include <app/server/Server.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <data-model-providers/codegen/Instance.h>
#include <lib/core/ErrorStr.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <system/SystemClock.h>

#ifdef CONFIG_NET_L2_OPENTHREAD
#include <platform/OpenThread/GenericNetworkCommissioningThreadDriver.h>
#endif

#ifdef CONFIG_WIFI_ESP32
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <platform/Zephyr/wifi/ZephyrWifiDriver.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_if.h>
#endif

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceLayer;

namespace {

constexpr int kFactoryResetTriggerTimeout     = 3000;
constexpr int kFactoryResetCancelWindowTimeout = 3000;
constexpr int kAppEventQueueSize              = 10;
constexpr EndpointId kLightEndpointId         = 1;
constexpr uint8_t kDefaultMinLevel            = 0;
constexpr uint8_t kDefaultMaxLevel            = 254;

// NOTE! This key is for test/certification only and should not be available in production devices!
uint8_t sTestEventTriggerEnableKey[TestEventTriggerDelegate::kEnableKeyLength] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                                                                   0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };

K_MSGQ_DEFINE(sAppEventQueue, sizeof(AppEvent), kAppEventQueueSize, alignof(AppEvent));
k_timer sFunctionTimer;

// LED GPIO specs — these will be populated from devicetree if available
// Use DT_ALIAS for board-agnostic LED references
#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
static const struct gpio_dt_spec sStatusLedSpec = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#endif

#if DT_NODE_HAS_STATUS(DT_ALIAS(led1), okay)
static const struct gpio_dt_spec sLightLedSpec = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
#endif

// Button GPIO specs
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
static const struct gpio_dt_spec sButtonSpec = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback sButtonCbData;
#endif

Identify sIdentify = { kLightEndpointId, AppTask::IdentifyStartHandler, AppTask::IdentifyStopHandler,
                       Clusters::Identify::IdentifyTypeEnum::kVisibleIndicator };

LEDWidget sStatusLED;
LEDWidget sLightLED;

bool sIsNetworkProvisioned = false;
bool sIsNetworkEnabled     = false;
bool sHaveBLEConnections   = false;

chip::DeviceLayer::DeviceInfoProviderImpl gExampleDeviceInfoProvider;

DeferredAttribute gCurrentLevelPersister(ConcreteAttributePath(kLightEndpointId, Clusters::LevelControl::Id,
                                                               Clusters::LevelControl::Attributes::CurrentLevel::Id));

DefaultAttributePersistenceProvider gSimpleAttributePersistence;
DeferredAttributePersistenceProvider gDeferredAttributePersister(gSimpleAttributePersistence,
                                                                 Span<DeferredAttribute>(&gCurrentLevelPersister, 1),
                                                                 System::Clock::Milliseconds32(5000));

#ifdef CONFIG_NET_L2_OPENTHREAD
Clusters::NetworkCommissioning::InstanceAndDriver<NetworkCommissioning::GenericThreadDriver> sThreadNetworkDriver(0 /*endpointId*/);
#endif

#ifdef CONFIG_WIFI_ESP32
app::Clusters::NetworkCommissioning::Instance sWiFiCommissioningInstance(0, &(NetworkCommissioning::ZephyrWifiDriver::Instance()));
#endif
} // namespace

namespace LedConsts {
constexpr uint32_t kBlinkRate_ms{ 500 };
constexpr uint32_t kIdentifyBlinkRate_ms{ 500 };

namespace StatusLed {
namespace Unprovisioned {
constexpr uint32_t kOn_ms{ 100 };
constexpr uint32_t kOff_ms{ kOn_ms };
} // namespace Unprovisioned
namespace Provisioned {
constexpr uint32_t kOn_ms{ 50 };
constexpr uint32_t kOff_ms{ 950 };
} // namespace Provisioned
} // namespace StatusLed
} // namespace LedConsts

CHIP_ERROR AppTask::Init()
{
    LOG_INF("Init CHIP stack");

#if defined(CONFIG_WIFI_ESP32)
    // Connect WiFi early — Matter needs network before server init
    {
        struct net_if * iface = net_if_get_default();
        static struct wifi_connect_req_params cnx_params = {};
        cnx_params.ssid = (const uint8_t *)"zephyr-ssid";
        cnx_params.ssid_length = strlen("zephyr-ssid");
        cnx_params.psk = (const uint8_t *)"zephyr-psk";
        cnx_params.psk_length = strlen("zephyr-psk");
        cnx_params.channel = WIFI_CHANNEL_ANY;
        cnx_params.security = WIFI_SECURITY_TYPE_PSK;
        cnx_params.band = WIFI_FREQ_BAND_UNKNOWN;
        cnx_params.mfp = WIFI_MFP_OPTIONAL;

        // Wait for WiFi driver to be ready
        k_msleep(1000);

        LOG_INF("WiFi connecting to zephyr-ssid...");
        int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params, sizeof(cnx_params));
        if (ret) {
            LOG_ERR("WiFi connect request failed: %d", ret);
        }

        // Wait for connection (up to 15 seconds)
        bool connected = false;
        for (int i = 0; i < 150; i++) {
            struct wifi_iface_status status = {};
            ret = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));
            if (ret == 0 && status.state >= WIFI_STATE_ASSOCIATED) {
                LOG_INF("WiFi associated! SSID: %s, RSSI: %d", status.ssid, status.rssi);
                connected = true;
                // Wait a bit more for IP
                k_msleep(2000);
                break;
            }
            if (i % 10 == 0) {
                LOG_INF("WiFi waiting... state=%d (%d/%d)", status.state, i, 150);
            }
            k_msleep(100);
        }
        if (!connected) {
            LOG_ERR("WiFi failed to connect within 15s");
        }
    }
#endif

    CHIP_ERROR err = chip::Platform::MemoryInit();
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("Platform::MemoryInit() failed");
        return err;
    }

    err = PlatformMgr().InitChipStack();
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("PlatformMgr().InitChipStack() failed");
        return err;
    }

#if defined(CONFIG_NET_L2_OPENTHREAD)
    err = ThreadStackMgr().InitThreadStack();
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("ThreadStackMgr().InitThreadStack() failed");
        return err;
    }

    err = ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_Router);
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("ConnectivityMgr().SetThreadDeviceType() failed");
        return err;
    }

    TEMPORARY_RETURN_IGNORED sThreadNetworkDriver.Init();
#elif defined(CONFIG_WIFI_ESP32)
    TEMPORARY_RETURN_IGNORED sWiFiCommissioningInstance.Init();
#else
    LOG_WRN("No network transport configured (Thread or WiFi)");
#endif

    // Initialize LEDs
    LEDWidget::SetStateUpdateCallback(LEDStateUpdateHandler);

#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
    sStatusLED.Init(&sStatusLedSpec);
#endif
#if DT_NODE_HAS_STATUS(DT_ALIAS(led1), okay)
    sLightLED.Init(&sLightLedSpec);
    sLightLED.Set(false);
#endif

    UpdateStatusLED();

    // Initialize button
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
    if (gpio_is_ready_dt(&sButtonSpec))
    {
        int ret = gpio_pin_configure_dt(&sButtonSpec, GPIO_INPUT);
        if (ret == 0)
        {
            ret = gpio_pin_interrupt_configure_dt(&sButtonSpec, GPIO_INT_EDGE_TO_ACTIVE);
        }
        if (ret == 0)
        {
            gpio_init_callback(&sButtonCbData, ButtonEventHandler, BIT(sButtonSpec.pin));
            gpio_add_callback(sButtonSpec.port, &sButtonCbData);
        }
        if (ret != 0)
        {
            LOG_ERR("Failed to configure button GPIO: %d", ret);
        }
    }
#endif

    // Initialize function button timer
    k_timer_init(&sFunctionTimer, &AppTask::FunctionTimerTimeoutCallback, nullptr);
    k_timer_user_data_set(&sFunctionTimer, this);

    // Initialize CHIP server
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());

    static CommonCaseDeviceServerInitParams initParams;
    static SimpleTestEventTriggerDelegate sTestEventTriggerDelegate{};
    static OTATestEventTriggerHandler sOtaTestEventTriggerHandler{};
    VerifyOrDie(sTestEventTriggerDelegate.Init(ByteSpan(sTestEventTriggerEnableKey)) == CHIP_NO_ERROR);
    VerifyOrDie(sTestEventTriggerDelegate.AddHandler(&sOtaTestEventTriggerHandler) == CHIP_NO_ERROR);

    (void) initParams.InitializeStaticResourcesBeforeServerInit();
    VerifyOrDie(gSimpleAttributePersistence.Init(initParams.persistentStorageDelegate) == CHIP_NO_ERROR);

    initParams.dataModelProvider        = CodegenDataModelProviderInstance(initParams.persistentStorageDelegate);
    initParams.testEventTriggerDelegate = &sTestEventTriggerDelegate;
    ReturnErrorOnFailure(chip::Server::GetInstance().Init(initParams));

    gExampleDeviceInfoProvider.SetStorageDelegate(&Server::GetInstance().GetPersistentStorage());
    chip::DeviceLayer::SetDeviceInfoProvider(&gExampleDeviceInfoProvider);
    app::SetAttributePersistenceProvider(&gDeferredAttributePersister);

    ConfigurationMgr().LogDeviceConfig();
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

    TEMPORARY_RETURN_IGNORED PlatformMgr().AddEventHandler(ChipEventHandler, 0);

    err = PlatformMgr().StartEventLoopTask();
    if (err != CHIP_NO_ERROR)
    {
        LOG_ERR("PlatformMgr().StartEventLoopTask() failed");
    }

    return err;
}

CHIP_ERROR AppTask::StartApp()
{
    ReturnErrorOnFailure(Init());

    AppEvent event = {};

    while (true)
    {
        k_msgq_get(&sAppEventQueue, &event, K_FOREVER);
        DispatchEvent(event);
    }

    return CHIP_NO_ERROR;
}

void AppTask::IdentifyStartHandler(Identify *)
{
    AppEvent event;
    event.Type    = AppEventType::IdentifyStart;
    event.Handler = [](const AppEvent &) { sLightLED.Blink(LedConsts::kIdentifyBlinkRate_ms); };
    PostEvent(event);
}

void AppTask::IdentifyStopHandler(Identify *)
{
    AppEvent event;
    event.Type    = AppEventType::IdentifyStop;
    event.Handler = [](const AppEvent &) { sLightLED.Set(AppTask::Instance().IsLightOn()); };
    PostEvent(event);
}

void AppTask::LightingActionEventHandler(const AppEvent & event)
{
    if (event.Type == AppEventType::Lighting)
    {
        bool on      = event.LightingEvent.Action != 0;
        uint8_t level = static_cast<uint8_t>(event.LightingEvent.Actor);
        Instance().SetLight(on, level);
    }
    else if (event.Type == AppEventType::Button)
    {
        Instance().SetLight(!Instance().IsLightOn(), Instance().GetLightLevel());
        Instance().UpdateClusterState();
    }
}

void AppTask::SetLight(bool on, uint8_t level)
{
    mIsLightOn  = on;
    mLightLevel = level;

#if DT_NODE_HAS_STATUS(DT_ALIAS(led1), okay)
    sLightLED.Set(on);
#endif

    if (on)
    {
        LOG_INF("Light ON (level %u)", level);
    }
    else
    {
        LOG_INF("Light OFF");
    }
}

void AppTask::ButtonEventHandler(const struct device * dev, struct gpio_callback * cb, uint32_t pins)
{
    AppEvent button_event;
    button_event.Type               = AppEventType::Button;
    button_event.ButtonEvent.PinNo  = 0;
    button_event.ButtonEvent.Action = static_cast<uint8_t>(AppEventType::ButtonPushed);
    button_event.Handler            = FunctionHandler;
    PostEvent(button_event);
}

void AppTask::FunctionTimerTimeoutCallback(k_timer * timer)
{
    if (!timer)
    {
        return;
    }

    AppEvent event;
    event.Type               = AppEventType::Timer;
    event.TimerEvent.Context = k_timer_user_data_get(timer);
    event.Handler            = FunctionTimerEventHandler;
    PostEvent(event);
}

void AppTask::FunctionTimerEventHandler(const AppEvent & event)
{
    if (event.Type != AppEventType::Timer)
    {
        return;
    }

    if (Instance().mFunction == FunctionEvent::SoftwareUpdate)
    {
        LOG_INF("Factory Reset Triggered. Release button within %ums to cancel.", kFactoryResetTriggerTimeout);
        Instance().StartTimer(kFactoryResetCancelWindowTimeout);
        Instance().mFunction = FunctionEvent::FactoryReset;

        sStatusLED.Set(false);
        sStatusLED.Blink(LedConsts::kBlinkRate_ms);
    }
    else if (Instance().mFunction == FunctionEvent::FactoryReset)
    {
        Instance().mFunction = FunctionEvent::NoneSelected;
        chip::Server::GetInstance().ScheduleFactoryReset();
    }
}

void AppTask::FunctionHandler(const AppEvent & event)
{
    if (event.ButtonEvent.Action == static_cast<uint8_t>(AppEventType::ButtonPushed))
    {
        if (!Instance().mFunctionTimerActive && Instance().mFunction == FunctionEvent::NoneSelected)
        {
            Instance().StartTimer(kFactoryResetTriggerTimeout);
            Instance().mFunction = FunctionEvent::SoftwareUpdate;
        }
    }
    else
    {
        if (Instance().mFunctionTimerActive && Instance().mFunction == FunctionEvent::SoftwareUpdate)
        {
            Instance().CancelTimer();
            Instance().mFunction = FunctionEvent::NoneSelected;
            LOG_INF("Software update is disabled");
        }
        else if (Instance().mFunctionTimerActive && Instance().mFunction == FunctionEvent::FactoryReset)
        {
            UpdateStatusLED();
            Instance().CancelTimer();
            Instance().mFunction = FunctionEvent::NoneSelected;
            LOG_INF("Factory Reset has been Canceled");
        }
    }
}

void AppTask::StartBLEAdvertisementHandler(const AppEvent &)
{
    if (Server::GetInstance().GetFabricTable().FabricCount() != 0)
    {
        LOG_INF("Matter service BLE advertising not started - device is already commissioned");
        return;
    }

    if (ConnectivityMgr().IsBLEAdvertisingEnabled())
    {
        LOG_INF("BLE advertising is already enabled");
        return;
    }

    if (Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow() != CHIP_NO_ERROR)
    {
        LOG_ERR("OpenBasicCommissioningWindow() failed");
    }
}

void AppTask::UpdateLedStateEventHandler(const AppEvent & event)
{
    if (event.Type == AppEventType::UpdateLedState)
    {
        event.UpdateLedStateEvent.LedWidget->UpdateState();
    }
}

void AppTask::LEDStateUpdateHandler(LEDWidget & ledWidget)
{
    AppEvent event;
    event.Type                          = AppEventType::UpdateLedState;
    event.Handler                       = UpdateLedStateEventHandler;
    event.UpdateLedStateEvent.LedWidget = &ledWidget;
    PostEvent(event);
}

void AppTask::UpdateStatusLED()
{
    if (sIsNetworkProvisioned && sIsNetworkEnabled)
    {
        sStatusLED.Set(true);
    }
    else if (sHaveBLEConnections)
    {
        sStatusLED.Blink(LedConsts::StatusLed::Unprovisioned::kOn_ms, LedConsts::StatusLed::Unprovisioned::kOff_ms);
    }
    else
    {
        sStatusLED.Blink(LedConsts::StatusLed::Provisioned::kOn_ms, LedConsts::StatusLed::Provisioned::kOff_ms);
    }
}

void AppTask::ChipEventHandler(const ChipDeviceEvent * event, intptr_t /* arg */)
{
    switch (event->Type)
    {
    case DeviceEventType::kCHIPoBLEAdvertisingChange:
        sHaveBLEConnections = ConnectivityMgr().NumBLEConnections() != 0;
        UpdateStatusLED();
        break;
#if defined(CONFIG_NET_L2_OPENTHREAD)
    case DeviceEventType::kDnssdInitialized:
        break;
    case DeviceEventType::kThreadStateChange:
        sIsNetworkProvisioned = ConnectivityMgr().IsThreadProvisioned();
        sIsNetworkEnabled     = ConnectivityMgr().IsThreadEnabled();
#elif defined(CONFIG_WIFI_ESP32)
    case DeviceEventType::kWiFiConnectivityChange:
        sIsNetworkProvisioned = ConnectivityMgr().IsWiFiStationProvisioned();
        sIsNetworkEnabled     = ConnectivityMgr().IsWiFiStationEnabled();
#endif
        UpdateStatusLED();
        break;
    default:
        break;
    }
}

void AppTask::CancelTimer()
{
    k_timer_stop(&sFunctionTimer);
    mFunctionTimerActive = false;
}

void AppTask::StartTimer(uint32_t timeoutInMs)
{
    k_timer_start(&sFunctionTimer, K_MSEC(timeoutInMs), K_NO_WAIT);
    mFunctionTimerActive = true;
}

void AppTask::PostEvent(const AppEvent & event)
{
    if (k_msgq_put(&sAppEventQueue, &event, K_NO_WAIT) != 0)
    {
        LOG_INF("Failed to post event to app task event queue");
    }
}

void AppTask::DispatchEvent(const AppEvent & event)
{
    if (event.Handler)
    {
        event.Handler(event);
    }
    else
    {
        LOG_INF("Event received with no handler. Dropping event.");
    }
}

void AppTask::UpdateClusterState()
{
    TEMPORARY_RETURN_IGNORED SystemLayer().ScheduleLambda([this] {
        Protocols::InteractionModel::Status status =
            Clusters::OnOff::Attributes::OnOff::Set(kLightEndpointId, mIsLightOn);

        if (status != Protocols::InteractionModel::Status::Success)
        {
            LOG_ERR("Updating on/off cluster failed: %x", to_underlying(status));
        }

        status = Clusters::LevelControl::Attributes::CurrentLevel::Set(kLightEndpointId, mLightLevel);

        if (status != Protocols::InteractionModel::Status::Success)
        {
            LOG_ERR("Updating level cluster failed: %x", to_underlying(status));
        }
    });
}
