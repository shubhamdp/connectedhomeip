/*
 *
 *    Copyright (c) 2021-2023 Project CHIP Authors
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

#include "DeviceCallbacks.h"

#include "AppTask.h"
#include "esp_log.h"
#include <common/CHIPDeviceManager.h>
#include <common/Esp32AppServer.h>
#include <common/Esp32ThreadInit.h>
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
#include "spi_flash_mmap.h"
#else
#include "esp_spi_flash.h"
#endif
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "shell_extension/launch.h"
#include "shell_extension/openthread_cli_register.h"
#include <app/server/Dnssd.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <platform/ESP32/ESP32Utils.h>
#include <setup_payload/OnboardingCodesUtil.h>

#if CONFIG_ENABLE_ESP_INSIGHTS_SYSTEM_STATS
#include <tracing/esp32_trace/insights_sys_stats.h>
#define START_TIMEOUT_MS 60000
#endif

#if CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif // CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER

#if CONFIG_ENABLE_PW_RPC
#include "Rpc.h"
#endif

#include "DeviceWithDisplay.h"

#if CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER
#include <platform/ESP32/ESP32DeviceInfoProvider.h>
#else
#include <DeviceInfoProviderImpl.h>
#endif // CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER

#if CONFIG_SEC_CERT_DAC_PROVIDER
#include <platform/ESP32/ESP32SecureCertDACProvider.h>
#endif

#if CONFIG_ENABLE_ESP_INSIGHTS_TRACE
#include <esp_insights.h>
#include <tracing/esp32_trace/esp32_tracing.h>
#include <tracing/registry.h>
#endif

#include <app/server/Server.h>

using namespace ::chip;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceManager;
using namespace ::chip::DeviceLayer;

#if CONFIG_ENABLE_ESP_INSIGHTS_TRACE
extern const char insights_auth_key_start[] asm("_binary_insights_auth_key_txt_start");
extern const char insights_auth_key_end[] asm("_binary_insights_auth_key_txt_end");
#endif

static const char TAG[] = "light-app";

static AppDeviceCallbacks EchoCallbacks;
static AppDeviceCallbacksDelegate sAppDeviceCallbacksDelegate;

namespace {
#if CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
DeviceLayer::ESP32FactoryDataProvider sFactoryDataProvider;
#endif // CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER

#if CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER
DeviceLayer::ESP32DeviceInfoProvider gExampleDeviceInfoProvider;
#else
DeviceLayer::DeviceInfoProviderImpl gExampleDeviceInfoProvider;
#endif // CONFIG_ENABLE_ESP32_DEVICE_INFO_PROVIDER

#if CONFIG_SEC_CERT_DAC_PROVIDER
DeviceLayer::ESP32SecureCertDACProvider gSecureCertDACProvider;
#endif // CONFIG_SEC_CERT_DAC_PROVIDER

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
extern const uint8_t cd_start[] asm("_binary_certification_declaration_der_start");
extern const uint8_t cd_end[] asm("_binary_certification_declaration_der_end");
ByteSpan cdSpan(cd_start, static_cast<size_t>(cd_end - cd_start));
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

chip::Credentials::DeviceAttestationCredentialsProvider * get_dac_provider(void)
{
#if CONFIG_SEC_CERT_DAC_PROVIDER
#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
    gSecureCertDACProvider.SetCertificationDeclaration(cdSpan);
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API
    return &gSecureCertDACProvider;
#elif CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
    sFactoryDataProvider.SetCertificationDeclaration(cdSpan);
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API
    return &sFactoryDataProvider;
#else  // EXAMPLE_DAC_PROVIDER
    return chip::Credentials::Examples::GetExampleDACProvider();
#endif
}

char * addr_str(const void * addr)
{
    static char buf[6 * 2 + 5 + 1];
    const uint8_t * u8p = static_cast<const uint8_t *>(addr);
    sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x", u8p[5], u8p[4], u8p[3], u8p[2], u8p[1], u8p[0]);
    return buf;
}

void print_bytes(const uint8_t * bytes, int len)
{
    printf("0x");
    for (int i = 0; i < len; i++)
    {
        printf("%02x", bytes[i]);
    }
    printf("\n");
}

void print_uuid(const ble_uuid_t * uuid)
{
    char buf[BLE_UUID_STR_LEN];
    printf("%s", ble_uuid_to_str(uuid, buf));
}

void print_adv_fields(const struct ble_hs_adv_fields * fields)
{
    char s[BLE_HS_ADV_MAX_SZ];
    const uint8_t * u8p;
    int i;

    if (fields->flags != 0)
    {
        printf("    flags=0x%02x\n", fields->flags);
    }

    if (fields->uuids16 != NULL)
    {
        printf("    uuids16(%scomplete)=", fields->uuids16_is_complete ? "" : "in");
        for (i = 0; i < fields->num_uuids16; i++)
        {
            print_uuid(&fields->uuids16[i].u);
            printf(" ");
        }
        printf("\n");
    }

    if (fields->uuids32 != NULL)
    {
        printf("    uuids32(%scomplete)=", fields->uuids32_is_complete ? "" : "in");
        for (i = 0; i < fields->num_uuids32; i++)
        {
            print_uuid(&fields->uuids32[i].u);
            printf(" ");
        }
        printf("\n");
    }

    if (fields->uuids128 != NULL)
    {
        printf("    uuids128(%scomplete)=", fields->uuids128_is_complete ? "" : "in");
        for (i = 0; i < fields->num_uuids128; i++)
        {
            print_uuid(&fields->uuids128[i].u);
            printf(" ");
        }
        printf("\n");
    }

    if (fields->name != NULL)
    {
        assert(fields->name_len < sizeof s - 1);
        memcpy(s, fields->name, fields->name_len);
        s[fields->name_len] = '\0';
        printf("    name(%scomplete)=%s\n", fields->name_is_complete ? "" : "in", s);
    }

    if (fields->tx_pwr_lvl_is_present)
    {
        printf("    tx_pwr_lvl=%d\n", fields->tx_pwr_lvl);
    }

    if (fields->slave_itvl_range != NULL)
    {
        printf("    slave_itvl_range=");
        print_bytes(fields->slave_itvl_range, BLE_HS_ADV_SLAVE_ITVL_RANGE_LEN);
        printf("\n");
    }

    if (fields->sm_tk_value_is_present)
    {
        printf("    sm_tk_value=");
        print_bytes(fields->sm_tk_value, 16);
        printf("\n");
    }

    if (fields->sm_oob_flag_is_present)
    {
        printf("    sm_oob_flag=%d\n", fields->sm_oob_flag);
    }

    if (fields->sol_uuids16 != NULL)
    {
        printf("    sol_uuids16=");
        for (i = 0; i < fields->sol_num_uuids16; i++)
        {
            print_uuid(&fields->sol_uuids16[i].u);
            printf(" ");
        }
        printf("\n");
    }

    if (fields->sol_uuids32 != NULL)
    {
        printf("    sol_uuids32=");
        for (i = 0; i < fields->sol_num_uuids32; i++)
        {
            print_uuid(&fields->sol_uuids32[i].u);
            printf("\n");
        }
        printf("\n");
    }

    if (fields->sol_uuids128 != NULL)
    {
        printf("    sol_uuids128=");
        for (i = 0; i < fields->sol_num_uuids128; i++)
        {
            print_uuid(&fields->sol_uuids128[i].u);
            printf(" ");
        }
        printf("\n");
    }

    if (fields->svc_data_uuid16 != NULL)
    {
        printf("    svc_data_uuid16=");
        print_bytes(fields->svc_data_uuid16, fields->svc_data_uuid16_len);
        printf("\n");
    }

    if (fields->public_tgt_addr != NULL)
    {
        printf("    public_tgt_addr=");
        u8p = fields->public_tgt_addr;
        for (i = 0; i < fields->num_public_tgt_addrs; i++)
        {
            printf("public_tgt_addr=%s ", addr_str(u8p));
            u8p += BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN;
        }
        printf("\n");
    }

    if (fields->random_tgt_addr != NULL)
    {
        printf("    random_tgt_addr=");
        u8p = fields->random_tgt_addr;
        for (i = 0; i < fields->num_random_tgt_addrs; i++)
        {
            printf("random_tgt_addr=%s ", addr_str(u8p));
            u8p += BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN;
        }
        printf("\n");
    }

    if (fields->appearance_is_present)
    {
        printf("    appearance=0x%04x\n", fields->appearance);
    }

    if (fields->adv_itvl_is_present)
    {
        printf("    adv_itvl=0x%04x\n", fields->adv_itvl);
    }

    if (fields->device_addr_is_present)
    {
        printf("    device_addr=");
        u8p = fields->device_addr;
        printf("%s ", addr_str(u8p));

        u8p += BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN;
        printf("addr_type %d ", *u8p);
    }

    if (fields->le_role_is_present)
    {
        printf("    le_role=%d\n", fields->le_role);
    }

    if (fields->svc_data_uuid32 != NULL)
    {
        printf("    svc_data_uuid32=");
        print_bytes(fields->svc_data_uuid32, fields->svc_data_uuid32_len);
        printf("\n");
    }

    if (fields->svc_data_uuid128 != NULL)
    {
        printf("    svc_data_uuid128=");
        print_bytes(fields->svc_data_uuid128, fields->svc_data_uuid128_len);
        printf("\n");
    }

    if (fields->uri != NULL)
    {
        printf("    uri=");
        print_bytes(fields->uri, fields->uri_len);
        printf("\n");
    }

    if (fields->mfg_data != NULL)
    {
        printf("    mfg_data=");
        print_bytes(fields->mfg_data, fields->mfg_data_len);
        printf("\n");
    }
}

static int blecent_gap_event(struct ble_gap_event * event, void * arg)
{
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (rc != 0)
        {
            return 0;
        }

        /* An advertisement report was received during GAP discovery. */
        print_adv_fields(&fields);
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "discovery complete; reason=%d\n", event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d\n", event->mtu.conn_handle, event->mtu.channel_id,
                 event->mtu.value);
        return 0;
    default:
        return 0;
    }
}

void StartBLEBeaconScanning()
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params;
    int rc;

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Tell the controller to filter duplicates; we don't want to process
     * repeated advertisements from the same device.
     */
    disc_params.filter_duplicates = 1;

    /**
     * Perform a passive scan.  I.e., don't send follow-up scan requests to
     * each advertiser.
     */
    disc_params.passive = 1;

    /* Use defaults for the rest of the parameters. */
    disc_params.itvl          = 0;
    disc_params.window        = 0;
    disc_params.filter_policy = 0;
    disc_params.limited       = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, blecent_gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error initiating GAP discovery procedure; rc=%d\n", rc);
    }
}

void CommissioningCompleteCallback(const ChipDeviceEvent * event, intptr_t arg)
{
    switch (event->Type)
    {
    case DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        StartBLEBeaconScanning();
        break;
    default:
        break;
    }
}

} // namespace

static void InitServer(intptr_t context)
{
    // Print QR Code URL
    PrintOnboardingCodes(chip::RendezvousInformationFlags(CONFIG_RENDEZVOUS_MODE));

    DeviceCallbacksDelegate::Instance().SetAppDelegate(&sAppDeviceCallbacksDelegate);
    Esp32AppServer::Init(); // Init ZCL Data Model and CHIP App Server AND Initialize device attestation config

    // If no fabric, wait for commissioning complete
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0)
    {
        PlatformMgr().AddEventHandler(CommissioningCompleteCallback, reinterpret_cast<intptr_t>(nullptr));
    }
    else
    {
        StartBLEBeaconScanning();
    }

#if CONFIG_ENABLE_ESP_INSIGHTS_TRACE
    esp_insights_config_t config = {
        .log_type = ESP_DIAG_LOG_TYPE_ERROR | ESP_DIAG_LOG_TYPE_WARNING | ESP_DIAG_LOG_TYPE_EVENT,
        .auth_key = insights_auth_key_start,
    };

    esp_err_t ret = esp_insights_init(&config);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize ESP Insights, err:0x%x", ret);
    }

    static Tracing::Insights::ESP32Backend backend;
    Tracing::Register(backend);

#if CONFIG_ENABLE_ESP_INSIGHTS_SYSTEM_STATS
    chip::System::Stats::InsightsSystemMetrics::GetInstance().RegisterAndEnable(chip::System::Clock::Timeout(START_TIMEOUT_MS));
#endif
#endif
}

extern "C" void app_main()
{
    // Initialize the ESP NVS layer.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init() failed: %s", esp_err_to_name(err));
        return;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_event_loop_create_default() failed: %s", esp_err_to_name(err));
        return;
    }
#if CONFIG_ENABLE_PW_RPC
    chip::rpc::Init();
#endif

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "chip-esp32-light-example starting");
    ESP_LOGI(TAG, "==================================================");

#if CONFIG_ENABLE_CHIP_SHELL
#if CONFIG_OPENTHREAD_CLI
    chip::RegisterOpenThreadCliCommands();
#endif
    chip::LaunchShell();
#endif
#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
    if (Internal::ESP32Utils::InitWiFiStack() != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "Failed to initialize WiFi stack");
        return;
    }
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI

    DeviceLayer::SetDeviceInfoProvider(&gExampleDeviceInfoProvider);

    CHIPDeviceManager & deviceMgr = CHIPDeviceManager::GetInstance();
    CHIP_ERROR error              = deviceMgr.Init(&EchoCallbacks);
    if (error != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "device.Init() failed: %s", ErrorStr(error));
        return;
    }

#if CONFIG_ENABLE_ESP32_FACTORY_DATA_PROVIDER
    SetCommissionableDataProvider(&sFactoryDataProvider);
#if CONFIG_ENABLE_ESP32_DEVICE_INSTANCE_INFO_PROVIDER
    SetDeviceInstanceInfoProvider(&sFactoryDataProvider);
#endif
#endif

    SetDeviceAttestationCredentialsProvider(get_dac_provider());

    chip::DeviceLayer::PlatformMgr().ScheduleWork(InitServer, reinterpret_cast<intptr_t>(nullptr));

    error = GetAppTask().StartAppTask();
    if (error != CHIP_NO_ERROR)
    {
        ESP_LOGE(TAG, "GetAppTask().StartAppTask() failed : %s", ErrorStr(error));
    }
}
