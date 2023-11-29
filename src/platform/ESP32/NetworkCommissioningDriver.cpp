/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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

#include <crypto/CHIPCryptoPALmbedTLS.h>
#include <lib/support/Base64.h>
#include <lib/support/BytesToHex.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/SafeInt.h>
#include <lib/support/SafePointerCast.h>
#include <mbedtls/pem.h>
#include <mbedtls/pk.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/ESP32Utils.h>
#include <platform/ESP32/NetworkCommissioningDriver.h>

#include "esp_wifi.h"
#include "esp_wpa2.h"

#include <limits>
#include <string>

// Move this out to a new file where we can guarantee that key is secure and not exposed here.
namespace chip {
namespace Crypto {

// P256 won't let us write the keypair in binary format i.e. DER
// and esp-wifi impl requires the private key in PEM format, hence
// extending the P256Keypair to add an option to export the keypair
// in DER and PEM format.
class PDCKeypair : public P256Keypair
{
public:
    enum class SerializationFormat
    {
        kFormatDer = 0,
        kFormatPem,
    };

    CHIP_ERROR SerializeToDer(MutableByteSpan & derKey) { return SerializeTo(SerializationFormat::kFormatDer, derKey); }

    CHIP_ERROR SerializeToPem(MutableByteSpan & pemKey) { return SerializeTo(SerializationFormat::kFormatPem, pemKey); }

private:
    CHIP_ERROR SerializeTo(SerializationFormat format, MutableByteSpan & key)
    {
        mbedtls_pk_context pk;
        pk.CHIP_CRYPTO_PAL_PRIVATE(pk_info) = mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY);
        pk.CHIP_CRYPTO_PAL_PRIVATE(pk_ctx)  = SafePointerCast<mbedtls_ecp_keypair *>(&mKeypair);

        int status = 0;
        if (format == SerializationFormat::kFormatDer)
        {
            status = mbedtls_pk_write_key_der(&pk, key.data(), key.size());
        }
        else
        {
            status = mbedtls_pk_write_key_pem(&pk, key.data(), key.size());
        }

        if (status != 0)
        {
            ChipLogError(DeviceLayer, "Failed to serialize the keypair to %s, status:%d",
                         format == SerializationFormat::kFormatDer ? "der" : "pem", status);
            return CHIP_ERROR_INTERNAL;
        }

        return CHIP_NO_ERROR;
    }
};

} // namespace Crypto
} // namespace chip

using namespace ::chip;
using namespace ::chip::DeviceLayer::Internal;
namespace chip {
namespace DeviceLayer {
namespace NetworkCommissioning {

namespace {
constexpr char kWiFiSSIDKeyName[]        = "wifi-ssid";
constexpr char kWiFiCredentialsKeyName[] = "wifi-pass";
static uint8_t WiFiSSIDStr[DeviceLayer::Internal::kMaxWiFiSSIDLength];
} // namespace

BitFlags<WiFiSecurityBitmap> ConvertSecurityType(wifi_auth_mode_t authMode)
{
    BitFlags<WiFiSecurityBitmap> securityType;
    switch (authMode)
    {
    case WIFI_AUTH_OPEN:
        securityType.Set(WiFiSecurity::kUnencrypted);
        break;
    case WIFI_AUTH_WEP:
        securityType.Set(WiFiSecurity::kWep);
        break;
    case WIFI_AUTH_WPA_PSK:
        securityType.Set(WiFiSecurity::kWpaPersonal);
        break;
    case WIFI_AUTH_WPA2_PSK:
        securityType.Set(WiFiSecurity::kWpa2Personal);
        break;
    case WIFI_AUTH_WPA_WPA2_PSK:
        securityType.Set(WiFiSecurity::kWpa2Personal);
        securityType.Set(WiFiSecurity::kWpaPersonal);
        break;
    case WIFI_AUTH_WPA3_PSK:
        securityType.Set(WiFiSecurity::kWpa3Personal);
        break;
    case WIFI_AUTH_WPA2_WPA3_PSK:
        securityType.Set(WiFiSecurity::kWpa3Personal);
        securityType.Set(WiFiSecurity::kWpa2Personal);
        break;
    default:
        break;
    }
    return securityType;
}

CHIP_ERROR GetConfiguredNetwork(Network & network)
{
    wifi_ap_record_t ap_info;
    esp_err_t err;
    err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK)
    {
        return chip::DeviceLayer::Internal::ESP32Utils::MapError(err);
    }
    static_assert(chip::DeviceLayer::Internal::kMaxWiFiSSIDLength <= UINT8_MAX, "SSID length might not fit in length");
    uint8_t length =
        static_cast<uint8_t>(strnlen(reinterpret_cast<const char *>(ap_info.ssid), DeviceLayer::Internal::kMaxWiFiSSIDLength));
    if (length > sizeof(network.networkID))
    {
        return CHIP_ERROR_INTERNAL;
    }
    memcpy(network.networkID, ap_info.ssid, length);
    network.networkIDLen = length;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ESPWiFiDriver::Init(NetworkStatusChangeCallback * networkStatusChangeCallback)
{
    CHIP_ERROR err;
    size_t ssidLen        = 0;
    size_t credentialsLen = 0;

    err = PersistedStorage::KeyValueStoreMgr().Get(kWiFiCredentialsKeyName, mSavedNetwork.credentials,
                                                   sizeof(mSavedNetwork.credentials), &credentialsLen);
    if (err == CHIP_ERROR_NOT_FOUND)
    {
        return CHIP_NO_ERROR;
    }

    err = PersistedStorage::KeyValueStoreMgr().Get(kWiFiSSIDKeyName, mSavedNetwork.ssid, sizeof(mSavedNetwork.ssid), &ssidLen);
    if (err == CHIP_ERROR_NOT_FOUND)
    {
        return CHIP_NO_ERROR;
    }
    if (!CanCastTo<uint8_t>(credentialsLen))
    {
        return CHIP_ERROR_INCORRECT_STATE;
    }
    mSavedNetwork.credentialsLen = static_cast<uint8_t>(credentialsLen);

    if (!CanCastTo<uint8_t>(ssidLen))
    {
        return CHIP_ERROR_INCORRECT_STATE;
    }
    mSavedNetwork.ssidLen = static_cast<uint8_t>(ssidLen);

    mStagingNetwork        = mSavedNetwork;
    mpScanCallback         = nullptr;
    mpConnectCallback      = nullptr;
    mpStatusChangeCallback = networkStatusChangeCallback;
    return err;
}

void ESPWiFiDriver::Shutdown()
{
    mpStatusChangeCallback = nullptr;
}

CHIP_ERROR ESPWiFiDriver::CommitConfiguration()
{
    ReturnErrorOnFailure(PersistedStorage::KeyValueStoreMgr().Put(kWiFiSSIDKeyName, mStagingNetwork.ssid, mStagingNetwork.ssidLen));
    ReturnErrorOnFailure(PersistedStorage::KeyValueStoreMgr().Put(kWiFiCredentialsKeyName, mStagingNetwork.credentials,
                                                                  mStagingNetwork.credentialsLen));
    mSavedNetwork = mStagingNetwork;
    return CHIP_NO_ERROR;
}

CHIP_ERROR ESPWiFiDriver::RevertConfiguration()
{
    mStagingNetwork = mSavedNetwork;
    return CHIP_NO_ERROR;
}

bool ESPWiFiDriver::NetworkMatch(const WiFiNetwork & network, ByteSpan networkId)
{
    return networkId.size() == network.ssidLen && memcmp(networkId.data(), network.ssid, network.ssidLen) == 0;
}

Status ESPWiFiDriver::AddOrUpdateNetwork(ByteSpan ssid, ByteSpan credentials, MutableCharSpan & outDebugText,
                                         uint8_t & outNetworkIndex)
{
    outDebugText.reduce_size(0);
    outNetworkIndex = 0;
    VerifyOrReturnError(mStagingNetwork.ssidLen == 0 || NetworkMatch(mStagingNetwork, ssid), Status::kBoundsExceeded);
    VerifyOrReturnError(credentials.size() <= sizeof(mStagingNetwork.credentials), Status::kOutOfRange);
    VerifyOrReturnError(ssid.size() <= sizeof(mStagingNetwork.ssid), Status::kOutOfRange);

    memcpy(mStagingNetwork.credentials, credentials.data(), credentials.size());
    mStagingNetwork.credentialsLen = static_cast<decltype(mStagingNetwork.credentialsLen)>(credentials.size());

    memcpy(mStagingNetwork.ssid, ssid.data(), ssid.size());
    mStagingNetwork.ssidLen = static_cast<decltype(mStagingNetwork.ssidLen)>(ssid.size());

    return Status::kSuccess;
}

Status ESPWiFiDriver::RemoveNetwork(ByteSpan networkId, MutableCharSpan & outDebugText, uint8_t & outNetworkIndex)
{
    outDebugText.reduce_size(0);
    outNetworkIndex = 0;
    VerifyOrReturnError(NetworkMatch(mStagingNetwork, networkId), Status::kNetworkIDNotFound);

    // Use empty ssid for representing invalid network
    mStagingNetwork.ssidLen = 0;
    return Status::kSuccess;
}

Status ESPWiFiDriver::ReorderNetwork(ByteSpan networkId, uint8_t index, MutableCharSpan & outDebugText)
{
    outDebugText.reduce_size(0);

    // Only one network is supported now
    VerifyOrReturnError(index == 0, Status::kOutOfRange);
    VerifyOrReturnError(NetworkMatch(mStagingNetwork, networkId), Status::kNetworkIDNotFound);
    return Status::kSuccess;
}

CHIP_ERROR ESPWiFiDriver::ConnectWiFiNetwork(const char * ssid, uint8_t ssidLen, const char * key, uint8_t keyLen)
{
    // If device is already connected to WiFi, then disconnect the WiFi,
    // clear the WiFi configurations and add the newly provided WiFi configurations.
    if (chip::DeviceLayer::Internal::ESP32Utils::IsStationProvisioned())
    {
        ChipLogProgress(DeviceLayer, "Disconnecting WiFi station interface");
        esp_err_t err = esp_wifi_disconnect();
        if (err != ESP_OK)
        {
            ChipLogError(DeviceLayer, "esp_wifi_disconnect() failed: %s", esp_err_to_name(err));
            return chip::DeviceLayer::Internal::ESP32Utils::MapError(err);
        }
        CHIP_ERROR error = chip::DeviceLayer::Internal::ESP32Utils::ClearWiFiStationProvision();
        if (error != CHIP_NO_ERROR)
        {
            ChipLogError(DeviceLayer, "ClearWiFiStationProvision failed: %s", chip::ErrorStr(error));
            return chip::DeviceLayer::Internal::ESP32Utils::MapError(err);
        }
    }

    ReturnErrorOnFailure(ConnectivityMgr().SetWiFiStationMode(ConnectivityManager::kWiFiStationMode_Disabled));

    wifi_config_t wifiConfig;

    // Set the wifi configuration
    memset(&wifiConfig, 0, sizeof(wifiConfig));
    memcpy(wifiConfig.sta.ssid, ssid, std::min(ssidLen, static_cast<uint8_t>(sizeof(wifiConfig.sta.ssid))));
    memcpy(wifiConfig.sta.password, key, std::min(keyLen, static_cast<uint8_t>(sizeof(wifiConfig.sta.password))));

    // Configure the ESP WiFi interface.
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifiConfig);
    if (err != ESP_OK)
    {
        ChipLogError(DeviceLayer, "esp_wifi_set_config() failed: %s", esp_err_to_name(err));
        return chip::DeviceLayer::Internal::ESP32Utils::MapError(err);
    }

    ReturnErrorOnFailure(ConnectivityMgr().SetWiFiStationMode(ConnectivityManager::kWiFiStationMode_Disabled));
    return ConnectivityMgr().SetWiFiStationMode(ConnectivityManager::kWiFiStationMode_Enabled);
}

void ESPWiFiDriver::OnConnectWiFiNetwork()
{
    if (mpConnectCallback)
    {
        DeviceLayer::SystemLayer().CancelTimer(OnConnectWiFiNetworkFailed, NULL);
        mpConnectCallback->OnResult(Status::kSuccess, CharSpan(), 0);
        mpConnectCallback = nullptr;
    }
}

void ESPWiFiDriver::OnConnectWiFiNetworkFailed()
{
    if (mpConnectCallback)
    {
        mpConnectCallback->OnResult(Status::kNetworkNotFound, CharSpan(), 0);
        mpConnectCallback = nullptr;
    }
}

void ESPWiFiDriver::OnConnectWiFiNetworkFailed(chip::System::Layer * aLayer, void * aAppState)
{
    CHIP_ERROR error = chip::DeviceLayer::Internal::ESP32Utils::ClearWiFiStationProvision();
    if (error != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "ClearWiFiStationProvision failed: %s", chip::ErrorStr(error));
    }
    ESPWiFiDriver::GetInstance().OnConnectWiFiNetworkFailed();
}

void ESPWiFiDriver::ConnectNetwork(ByteSpan networkId, ConnectCallback * callback)
{
    CHIP_ERROR err          = CHIP_NO_ERROR;
    Status networkingStatus = Status::kSuccess;
    Network configuredNetwork;
    const uint32_t secToMiliSec = 1000;

    // VerifyOrExit(NetworkMatch(mStagingNetwork, networkId), networkingStatus = Status::kNetworkIDNotFound);
    VerifyOrExit(mpConnectCallback == nullptr, networkingStatus = Status::kUnknownError);
    ChipLogProgress(NetworkProvisioning, "ESP NetworkCommissioningDelegate: SSID: %.*s", static_cast<int>(networkId.size()),
                    networkId.data());
    // if (CHIP_NO_ERROR == GetConfiguredNetwork(configuredNetwork))
    // {
    //     if (NetworkMatch(mStagingNetwork, ByteSpan(configuredNetwork.networkID, configuredNetwork.networkIDLen)))
    //     {
    //         if (callback)
    //         {
    //             callback->OnResult(Status::kSuccess, CharSpan(), 0);
    //         }
    //         return;
    //     }
    // }

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
    if (mStagingNetwork.networkIdentityLength > 0)
    {
        err = ConnectWiFiNetworkWithPDC();
    }
    else
    {
        err = ConnectWiFiNetwork(reinterpret_cast<const char *>(mStagingNetwork.ssid), mStagingNetwork.ssidLen,
                                 reinterpret_cast<const char *>(mStagingNetwork.credentials), mStagingNetwork.credentialsLen);
    }
#else
    err = ConnectWiFiNetwork(reinterpret_cast<const char *>(mStagingNetwork.ssid), mStagingNetwork.ssidLen,
                             reinterpret_cast<const char *>(mStagingNetwork.credentials), mStagingNetwork.credentialsLen);
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC

    err = DeviceLayer::SystemLayer().StartTimer(
        static_cast<System::Clock::Timeout>(kWiFiConnectNetworkTimeoutSeconds * secToMiliSec), OnConnectWiFiNetworkFailed, NULL);
    mpConnectCallback = callback;

exit:
    if (err != CHIP_NO_ERROR)
    {
        networkingStatus = Status::kUnknownError;
    }
    if (networkingStatus != Status::kSuccess)
    {
        ChipLogError(NetworkProvisioning, "Failed to connect to WiFi network:%s", chip::ErrorStr(err));
        mpConnectCallback = nullptr;
        callback->OnResult(networkingStatus, CharSpan(), 0);
    }
}

CHIP_ERROR ESPWiFiDriver::StartScanWiFiNetworks(ByteSpan ssid)
{
    esp_err_t err = ESP_OK;
    if (!ssid.empty())
    {
        wifi_scan_config_t scan_config = { 0 };
        memset(WiFiSSIDStr, 0, sizeof(WiFiSSIDStr));
        memcpy(WiFiSSIDStr, ssid.data(), ssid.size());
        scan_config.ssid = WiFiSSIDStr;
        err              = esp_wifi_scan_start(&scan_config, false);
    }
    else
    {
        err = esp_wifi_scan_start(NULL, false);
    }
    if (err != ESP_OK)
    {
        return chip::DeviceLayer::Internal::ESP32Utils::MapError(err);
    }
    return CHIP_NO_ERROR;
}

void ESPWiFiDriver::OnScanWiFiNetworkDone()
{
    if (!mpScanCallback)
    {
        ChipLogProgress(DeviceLayer, "No scan callback");
        return;
    }
    uint16_t ap_number;
    esp_wifi_scan_get_ap_num(&ap_number);
    if (!ap_number)
    {
        ChipLogProgress(DeviceLayer, "No AP found");
        mpScanCallback->OnFinished(Status::kSuccess, CharSpan(), nullptr);
        mpScanCallback = nullptr;
        return;
    }
    std::unique_ptr<wifi_ap_record_t[]> ap_buffer_ptr(new wifi_ap_record_t[ap_number]);
    if (ap_buffer_ptr == NULL)
    {
        ChipLogError(DeviceLayer, "can't malloc memory for ap_list_buffer");
        mpScanCallback->OnFinished(Status::kUnknownError, CharSpan(), nullptr);
        mpScanCallback = nullptr;
        return;
    }
    wifi_ap_record_t * ap_list_buffer = ap_buffer_ptr.get();
    if (esp_wifi_scan_get_ap_records(&ap_number, ap_list_buffer) == ESP_OK)
    {
        if (CHIP_NO_ERROR == DeviceLayer::SystemLayer().ScheduleLambda([ap_number, ap_list_buffer]() {
                std::unique_ptr<wifi_ap_record_t[]> auto_free(ap_list_buffer);
                ESPScanResponseIterator iter(ap_number, ap_list_buffer);
                if (GetInstance().mpScanCallback)
                {
                    GetInstance().mpScanCallback->OnFinished(Status::kSuccess, CharSpan(), &iter);
                    GetInstance().mpScanCallback = nullptr;
                }
                else
                {
                    ChipLogError(DeviceLayer, "can't find the ScanCallback function");
                }
            }))
        {
            ap_buffer_ptr.release();
        }
        else
        {
            ChipLogError(DeviceLayer, "can't schedule the scan result processing");
            mpScanCallback->OnFinished(Status::kUnknownError, CharSpan(), nullptr);
            mpScanCallback = nullptr;
        }
    }
    else
    {
        ChipLogError(DeviceLayer, "can't get ap_records ");
        mpScanCallback->OnFinished(Status::kUnknownError, CharSpan(), nullptr);
        mpScanCallback = nullptr;
    }
}

void ESPWiFiDriver::OnNetworkStatusChange()
{
    Network configuredNetwork;
    bool staEnabled = false, staConnected = false;
    VerifyOrReturn(ESP32Utils::IsStationEnabled(staEnabled) == CHIP_NO_ERROR);
    VerifyOrReturn(staEnabled && mpStatusChangeCallback != nullptr);
    CHIP_ERROR err = GetConfiguredNetwork(configuredNetwork);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Failed to get configured network when updating network status: %s", err.AsString());
        return;
    }
    VerifyOrReturn(ESP32Utils::IsStationConnected(staConnected) == CHIP_NO_ERROR);
    if (staConnected)
    {
        mpStatusChangeCallback->OnNetworkingStatusChange(
            Status::kSuccess, MakeOptional(ByteSpan(configuredNetwork.networkID, configuredNetwork.networkIDLen)), NullOptional);
        return;
    }

    // The disconnect reason for networking status changes is allowed to have
    // manufacturer-specific values, which is why it's an int32_t, even though
    // we just store a uint16_t value in it.
    int32_t lastDisconnectReason = GetLastDisconnectReason();
    mpStatusChangeCallback->OnNetworkingStatusChange(
        Status::kUnknownError, MakeOptional(ByteSpan(configuredNetwork.networkID, configuredNetwork.networkIDLen)),
        MakeOptional(lastDisconnectReason));
}

void ESPWiFiDriver::ScanNetworks(ByteSpan ssid, WiFiDriver::ScanCallback * callback)
{
    if (callback != nullptr)
    {
        mpScanCallback = callback;
        if (StartScanWiFiNetworks(ssid) != CHIP_NO_ERROR)
        {
            mpScanCallback = nullptr;
            callback->OnFinished(Status::kUnknownError, CharSpan(), nullptr);
        }
    }
}

CHIP_ERROR ESPWiFiDriver::SetLastDisconnectReason(const ChipDeviceEvent * event)
{
    VerifyOrReturnError(event->Type == DeviceEventType::kESPSystemEvent && event->Platform.ESPSystemEvent.Base == WIFI_EVENT &&
                            event->Platform.ESPSystemEvent.Id == WIFI_EVENT_STA_DISCONNECTED,
                        CHIP_ERROR_INVALID_ARGUMENT);
    mLastDisconnectedReason = event->Platform.ESPSystemEvent.Data.WiFiStaDisconnected.reason;
    return CHIP_NO_ERROR;
}

uint16_t ESPWiFiDriver::GetLastDisconnectReason()
{
    return mLastDisconnectedReason;
}

size_t ESPWiFiDriver::WiFiNetworkIterator::Count()
{
    return mDriver->mStagingNetwork.ssidLen == 0 ? 0 : 1;
}

bool ESPWiFiDriver::WiFiNetworkIterator::Next(Network & item)
{
    if (mExhausted || mDriver->mStagingNetwork.ssidLen == 0)
    {
        return false;
    }
    memcpy(item.networkID, mDriver->mStagingNetwork.ssid, mDriver->mStagingNetwork.ssidLen);
    item.networkIDLen = mDriver->mStagingNetwork.ssidLen;
    item.connected    = false;
    mExhausted        = true;

    Network configuredNetwork;
    CHIP_ERROR err = GetConfiguredNetwork(configuredNetwork);
    if (err == CHIP_NO_ERROR)
    {
        bool isConnected = false;
        err              = ESP32Utils::IsStationConnected(isConnected);
        if (err == CHIP_NO_ERROR && isConnected && configuredNetwork.networkIDLen == item.networkIDLen &&
            memcmp(configuredNetwork.networkID, item.networkID, item.networkIDLen) == 0)
        {
            item.connected = true;
        }
    }
    return true;
}

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC
bool ESPWiFiDriver::SupportsPerDeviceCredentials()
{
    return true;
}

// We need some sort of WiFiCredentialStore for private key?

static CHIP_ERROR CHIPCertToX509Pem(const ByteSpan chipCert, ByteSpan & pemCert);

CHIP_ERROR ESPWiFiDriver::AddOrUpdateNetworkWithPDC(ByteSpan ssid, ByteSpan networkIdentity,
                                                    Optional<uint8_t> clientIdentityNetworkIndex, Status & outStatus,
                                                    MutableCharSpan & outDebugText, MutableByteSpan & outClientIdentity,
                                                    uint8_t & outNetworkIndex)
{
    // verify the SSID
    // VerifyOrReturnError(mStagingNetwork.ssidLen == 0 || NetworkMatch(mStagingNetwork, ssid), CHIP_ERROR_INCORRECT_STATE,
    //                     outStatus = Status::kBoundsExceeded);
    // VerifyOrReturnError(mStagingNetwork.ssidLen == 0 || NetworkMatch(mStagingNetwork, ssid), CHIP_ERROR_INCORRECT_STATE,
    //                     outStatus = Status::kBoundsExceeded);
    // VerifyOrReturnError(ssid.size() <= sizeof(mStagingNetwork.ssid), CHIP_ERROR_INCORRECT_STATE, outStatus =
    // Status::kOutOfRange);
    // verify the networkIdentity
    VerifyOrReturnError(networkIdentity.size() <= Credentials::kMaxCHIPCompactNetworkIdentityLength, CHIP_ERROR_INCORRECT_STATE,
                        outStatus = Status::kOutOfRange);

    // No debug text
    outDebugText.reduce_size(0);
    outNetworkIndex = 0;

    // save the ssid
    memcpy(mStagingNetwork.ssid, ssid.data(), ssid.size());
    mStagingNetwork.ssidLen = static_cast<decltype(mStagingNetwork.ssidLen)>(ssid.size());
    // save the compact networkIdentity
    memcpy(mStagingNetwork.networkIdentity, networkIdentity.data(), networkIdentity.size());
    mStagingNetwork.networkIdentityLength = networkIdentity.size();

    // Generate a P256 Keypair
    // Here, we may have to generate key out of here and then use that to initialzie the P256Keypair
    // or someclass on top of P256Keypair which writes the private key in PEM format
    Crypto::PDCKeypair keypair;
    CHIP_ERROR err = keypair.Initialize(Crypto::ECPKeyTarget::ECDSA);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Failed to initialize the keypair, err:%" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    // This is temporary, needs to be fixed
    err = keypair.Serialize(mStagingNetwork.serializedKeypair);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Failed to serialize the keypair, err:%" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    // Network Client Identity
    MutableByteSpan compactClientIdentity(mStagingNetwork.networkClientIdentity);
    err = Credentials::NewChipNetworkIdentity(keypair, compactClientIdentity);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Failed to generate the new network identity, err:%" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }
    mStagingNetwork.networkClientIdentityLength = compactClientIdentity.size();

    // remove remove remove after POC
    ByteSpan networkClientIdentity(mStagingNetwork.networkClientIdentity, mStagingNetwork.networkClientIdentityLength);
    ByteSpan networkClientIdentityPem;
    ReturnErrorOnFailure(CHIPCertToX509Pem(networkClientIdentity, networkClientIdentityPem));
    ChipLogError(DeviceLayer, "dumping the cert");
    ChipLogError(DeviceLayer, "cert - %.*s", networkClientIdentityPem.size(), (char *) networkClientIdentityPem.data());
    // remove remove remove

    memcpy(outClientIdentity.data(), compactClientIdentity.data(), compactClientIdentity.size());
    outClientIdentity.reduce_size(compactClientIdentity.size());

    outStatus = Status::kSuccess;

    return CHIP_NO_ERROR;
}

#define PEM_CERT_BEGIN_HDR "-----BEGIN CERTIFICATE-----"
#define PEM_CERT_END_HDR "-----END CERTIFICATE-----"

#define PEM_EC_KEY_BEGIN_HDR "-----BEGIN EC PRIVATE KEY-----"
#define PEM_EC_KEY_END_HDR "-----END EC PRIVATE KEY-----"

static size_t GetPemSize(const char * header, const char * footer, size_t derCertLen)
{
    return strlen(header) + strlen(footer) + BASE64_ENCODED_LEN(derCertLen) + 1;
}

static CHIP_ERROR ConvertDerToPem(const char * header, const char * footer, const ByteSpan & derCert, MutableByteSpan & pemCert)
{
    size_t outLen;
    int status = mbedtls_pem_write_buffer(header, footer, derCert.data(), derCert.size(), pemCert.data(), pemCert.size(), &outLen);
    printf("mbedtls_pem_write_buffer - %d\n\n", status);
    VerifyOrReturnError(status == 0, CHIP_ERROR_INTERNAL);
    pemCert.reduce_size(outLen);
    return CHIP_NO_ERROR;
}

// This function dynamically allocates the pemCert.data(), free the pemCert.data() after use
static CHIP_ERROR CHIPCertToX509Pem(const ByteSpan chipCert, ByteSpan & pemCert)
{
    ESP_LOGE(TAG, "Inside CHIPCertToX509Pem");
    uint8_t derBuffer[Credentials::kMaxDERCertLength];
    MutableByteSpan derCert(derBuffer, Credentials::kMaxDERCertLength);

    CHIP_ERROR err = Credentials::ConvertChipCertToX509Cert(chipCert, derCert);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Failed to convert chip cert to x509 cert, err:%" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    ChipLogError(DeviceLayer, "CHIPCertToX509Pem - Dumping der");
    for (size_t i = 0; i < derCert.size(); i++)
    {
        printf("%02x", derCert.data()[i]);
    }
    printf("\n\n");

    size_t pemBufferLen   = GetPemSize(PEM_CERT_BEGIN_HDR, PEM_CERT_END_HDR, derCert.size()) + 64;
    uint8_t * pemBuffer   = (uint8_t *) malloc(pemBufferLen); // +64 rn for newlines, mbedtls_pem_write_buffer gives out the lengh
                                                            // required, so we can use that
    // VerifyOrReturnError(pemBuffer != nullptr, CHIP_ERROR_NO_MEMORY);
    if (!pemBuffer)
    {
        ChipLogError(DeviceLayer, "Failed to allocate buffer to store pem formatted certificate");
        return CHIP_ERROR_NO_MEMORY;
    }

    MutableByteSpan mtPemCert(pemBuffer, pemBufferLen);
    err = ConvertDerToPem(PEM_CERT_BEGIN_HDR, PEM_CERT_END_HDR, derCert, mtPemCert);
    // VerifyOrReturnError(err != CHIP_NO_ERROR, err, free(pemBuffer));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(DeviceLayer, "Failed to convert der to pem, err:%" CHIP_ERROR_FORMAT, err.Format());
        free(pemBuffer);
        return err;
    }

    pemCert = ByteSpan(mtPemCert.data(), mtPemCert.size());
    return CHIP_NO_ERROR;
}

CHIP_ERROR ESPWiFiDriver::ConnectWiFiNetworkWithPDC()
{
    // esp_wifi_set_vendor_ie_cb((esp_vendor_ie_cb_t)matter_vendor_ie_cb, NULL);
    CHIP_ERROR err = CHIP_NO_ERROR;

    esp_wifi_restore();
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    // mStagingNetwork.network....PEM = ... could be potential leaks, but keeping it for time being to test out stuff
#ifdef CONFIG_EXAMPLE_VALIDATE_SERVER_CERT
    // Server cert pem
    ByteSpan networkIdentity(mStagingNetwork.networkIdentity, mStagingNetwork.networkIdentityLength);
    ByteSpan networkIdentityPem();
    ReturnErrorOnFailure(CHIPCertToX509Pem(networkIdentity, networkIdentityPem));
    mStagingNetwork.networkIdentityCertPEM = networkIdentityPem.data();

#endif /* CONFIG_EXAMPLE_VALIDATE_SERVER_CERT */

    // Client cert pem
    ByteSpan networkClientIdentity(mStagingNetwork.networkClientIdentity, mStagingNetwork.networkClientIdentityLength);
    ByteSpan networkClientIdentityPem;
    ReturnErrorOnFailure(CHIPCertToX509Pem(networkClientIdentity, networkClientIdentityPem));
    // free the mStagingNetwork.networkIdentityCertPEM on failure
    mStagingNetwork.networkClientIdentityCertPEM = networkClientIdentityPem.data();

    // Client key pem, we can use chip library MemoryAllocate() than malloc
    uint8_t * keyPem = (uint8_t *) malloc(600);
    // free above two buffers
    VerifyOrReturnError(keyPem != nullptr, CHIP_ERROR_NO_MEMORY);
    MutableByteSpan networkClientKeyPem(keyPem, 600);

    Crypto::PDCKeypair keypair;
    keypair.Deserialize(mStagingNetwork.serializedKeypair);

    keypair.SerializeToPem(networkClientKeyPem);
    mStagingNetwork.networkClientIdentityKeyPEM = networkClientKeyPem.data();
    printf("private key - %.*s\n", networkClientKeyPem.size(), networkClientKeyPem.data());

    ReturnErrorOnFailure(ConnectivityMgr().SetWiFiStationMode(ConnectivityManager::kWiFiStationMode_Disabled));

    // Why RAM?
    // ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config;
    memcpy(wifi_config.sta.ssid, mStagingNetwork.ssid, mStagingNetwork.ssidLen);
    wifi_config.sta.matter_wifi_auth_enabled = true;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);

    uint8_t networkKeyIdentifierBuffer[20];
    Credentials::MutableCertificateKeyId networkKeyIdentifier(networkKeyIdentifierBuffer);
    // strlen(".pdc.csa-iot.org") = 16
    char EAPNetworkAccessIdentifier[40 + 16];
    const char * naiSuffix = ".pdc.csa-iot.org";

    // Credentials::ExtractIdentifierFromChipNetworkIdentity(
    //     { mStagingNetwork.networkIdentity, mStagingNetwork.networkIdentityLength }, networkKeyIdentifier);
    // Encoding::BytesToUppercaseHexBuffer(networkKeyIdentifier.data(), networkKeyIdentifier.size(), EAPNetworkAccessIdentifier,
    // 40); memcpy(EAPNetworkAccessIdentifier + 40, naiSuffix, strlen(naiSuffix));

    // esp_wifi_sta_wpa2_ent_set_identity((uint8_t *) EAPNetworkAccessIdentifier, sizeof(EAPNetworkAccessIdentifier));
    esp_wifi_sta_wpa2_ent_set_identity((uint8_t *) naiSuffix, strlen(naiSuffix));

    // check for error codes

    // Commenting this, we need some sort of validation for server when connecting to the newtork
    // #if defined(CONFIG_EXAMPLE_VALIDATE_SERVER_CERT) || defined(CONFIG_EXAMPLE_WPA3_ENTERPRISE) ||
    // defined(CONFIG_EXAMPLE_WPA3_192BIT_ENTERPRISE)
    // esp_wifi_sta_wpa2_ent_set_ca_cert(networkIdentityPem.data(), networkIdentityPem.data());
    // #endif /* CONFIG_EXAMPLE_VALIDATE_SERVER_CERT */ /* EXAMPLE_WPA3_ENTERPRISE */

#ifdef CONFIG_EXAMPLE_EAP_METHOD_TLS
        esp_wifi_sta_wpa2_ent_set_cert_key(networkClientIdentityPem.data(), networkClientIdentityPem.size(),
                                           networkIdentityKeyPem.data(), networkIdentityKeyPem.size(), NULL, 0);
#endif /* CONFIG_EXAMPLE_EAP_METHOD_TLS */

#if defined(CONFIG_EXAMPLE_WPA3_192BIT_ENTERPRISE)
    ESP_LOGI(TAG, "Enabling 192 bit certification");
    ESP_ERROR_CHECK(esp_wifi_sta_wpa2_set_suiteb_192bit_certification(true));
#endif

    // This validation can not be used, since server will be using the self signed certificate and default cert bundle
    // won't be able to validate it.
    // #ifdef CONFIG_EXAMPLE_USE_DEFAULT_CERT_BUNDLE
    //     ESP_ERROR_CHECK(esp_wifi_sta_wpa2_use_default_cert_bundle(true));
    // #endif

    esp_wifi_sta_wpa2_ent_enable();

    return ConnectivityMgr().SetWiFiStationMode(ConnectivityManager::kWiFiStationMode_Enabled);
}

CHIP_ERROR ESPWiFiDriver::GetNetworkIdentity(uint8_t networkIndex, MutableByteSpan & outNetworkIdentity)
{
    // This is the configured network identity, supposed to be stored when one calls AddOrUpdate...
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR ESPWiFiDriver::GetClientIdentity(uint8_t networkIndex, MutableByteSpan & outClientIdentity)
{
    // Shall we read from the NVS/ where to store ?
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR ESPWiFiDriver::SignWithClientIdentity(uint8_t networkIndex, ByteSpan & message,
                                                 Crypto::P256ECDSASignature & outSignature)
{
    // sign the message
    return CHIP_ERROR_NOT_IMPLEMENTED;
}
#endif // CHIP_DEVICE_CONFIG_ENABLE_WIFI_PDC

} // namespace NetworkCommissioning
} // namespace DeviceLayer
} // namespace chip
