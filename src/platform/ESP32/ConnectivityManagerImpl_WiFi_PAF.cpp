/*
 *
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

/* this file behaves like a config.h, comes first */
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <platform/ConnectivityManager.h>

#include <lib/support/CHIPMemString.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/DeviceInstanceInfoProvider.h>

#include "esp_wifi.h"
#include "esp_nan.h"
#include "esp_event.h"

using namespace chip;

#define SERVICE_NAME "_matterc._udp"

/* NAN-USD Service Protocol Type: ref: Table 58 of Wi-Fi Aware Specificaiton */
#define MAX_PAF_PUBLISH_SSI_BUFLEN 512
#define MAX_PAF_TX_SSI_BUFLEN 2048
#define NAM_PUBLISH_PERIOD 300u
#define NAN_PUBLISH_SSI_TAG " ssi="

#pragma pack(push, 1)
struct PAFPublishSSI
{
    uint8_t DevOpCode;
    uint16_t DevInfo;
    uint16_t ProductId;
    uint16_t VendorId;
};
#pragma pack(pop)

CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFPublish(ConnectivityManager::WiFiPAFAdvertiseParam & InArgs)
{
    struct PAFPublishSSI PafPublish_ssi;

    VerifyOrReturnError(
        (strlen(args) + strlen(NAN_PUBLISH_SSI_TAG) + (sizeof(struct PAFPublishSSI) * 2) < MAX_PAF_PUBLISH_SSI_BUFLEN),
        CHIP_ERROR_BUFFER_TOO_SMALL);

    PafPublish_ssi.DevOpCode = 0;

    VerifyOrDie(DeviceLayer::GetCommissionableDataProvider()->GetSetupDiscriminator(PafPublish_ssi.DevInfo) == CHIP_NO_ERROR);

    if (DeviceLayer::GetDeviceInstanceInfoProvider()->GetProductId(PafPublish_ssi.ProductId) != CHIP_NO_ERROR)
    {
        PafPublish_ssi.ProductId = 0;
    }

    if (DeviceLayer::GetDeviceInstanceInfoProvider()->GetVendorId(PafPublish_ssi.VendorId) != CHIP_NO_ERROR)
    {
        PafPublish_ssi.VendorId = 0;
    }

    // TODO: We may need to move this to wifi init phase
    wifi_nan_config_t nanConfig;
    nanConfig.usd_enabled = true;
    esp_err_t err = esp_wifi_nan_start(&nanConfig);
    VerfiyOrReturnError(err == ESP_OK, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "esp_wifi_nan_start failed, esp_err:%d", err));

    // TODO: Some parameters should be configurable somehow
    wifi_nan_publish_cfg_t publishConfig;
    memset(&publishConfig, 0, sizeof(publishConfig));

    Platform::CopyString(publishConfig.service_name, SERVICE_NAME);
    publishConfig.type = NAN_PUBLISH_UNSOLICITED | NAN_PUBLISH_SOLICITED;
    publishConfig.srv_proto_type = PROTOCOL_CSA_MATTER;
    publishConfig.ssi = static_cast<uint8_t *>(&PafPublish_ssi);
    publishConfig.ssi_len = sizeof(PafPublish_ssi);
    /*publishConfig.ttl = ;*/
    /*publishConfig.usd_chan_list = ;*/
    /*publishConfig.usd_chan_list_len = ;*/

    mNanPublishId = esp_wifi_nan_publish_service(&publishConfig, false /* ndp_resp_needed */);
    VerifyOrReturnError(publishId != 0, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "esp_wifi_nan_publish_service failed"));

    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFCancelPublish()
{
    esp_err_t err = esp_wifi_nan_cancel_publish(mNanPublishId);
    VerifyOrReturnError(err == ESP_OK, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "esp_wifi_nan_cancel_publish failed"));
    return ESP_OK;
}

CHIP_ERROR ConnectivityManagerImpl::_SetWiFiPAFAdvertisingEnabled(WiFiPAFAdvertiseParam & args)
{
    return args.enable ? _WiFiPAFPublish(args) : _WiFiPAFCancelPublish();
}

Transport::WiFiPAFBase * ConnectivityManagerImpl::_GetWiFiPAF()
{
    return pmWiFiPAF;
}

void ConnectivityManagerImpl::_SetWiFiPAF(Transport::WiFiPAFBase * pWiFiPAF)
{
    pmWiFiPAF = pWiFiPAF;
    return;
}

CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFConnect(const SetupDiscriminator & connDiscriminator, void * appState, OnConnectionCompleteFunct onSuccess,
                           OnConnectionErrorFunct onError)
{
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFCancelConnect()
{
    return CHIP_ERROR_NOT_IMPLEMENTED;
}

void ConnectivityManagerImpl::OnNanReceived(wifi_event_nan_receive_t * eventData)
{
    ChipLogProgress(DeviceLayer, "Our service identifier: %u", eventData->inst_id);
    ChipLogProgress(DeviceLayer, "Peer service identifier: %u", eventData->peer_inst_id);
    ChipLogProgress(DeviceLayer, "peer mac " MACSTR, MAC2STR(eventData->peer_if_mac));
    ChipLogProgress(DeviceLayer, "ssi len: %u", eventData->ssi_len);

    VerifyOrReturn(eventData->ssi_len > 0, ChipLogError(DeviceLayer, "SSI length is zero"));

    System::PacketBufferHandle buf;
    buf = System::PacketBufferHandle::NewWithData(eventData->peer_svc_info, eventData->ssi_len);

      // Post an event to the Chip queue to deliver the data into the Chip stack.
    ChipDeviceEvent event;
    event.Type                           = DeviceEventType::kCHIPoWiFiPAFWriteReceived;
    event.CHIPoWiFiPAFWriteReceived.Data = std::move(buf).UnsafeRelease();
    PlatformMgr().PostEventOrDie(&event); // why die here?
}

CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFSend(chip::System::PacketBufferHandle && msgBuf)
{
    ChipLogProgress(DeviceLayer, "WiFi-PAF: Sending %lu bytes", msgBuf->DataLength());

    CHIP_ERROR ret = CHIP_NO_ERROR;

    if (msgBuf.IsNull())
    {
        ChipLogError(DeviceLayer, "WiFi-PAF: Invalid Packet (%lu)", msgBuf->DataLength());
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    // Ensure outgoing message fits in a single contiguous packet buffer, as currently required by the
    // message fragmentation and reassembly engine.
    if (msgBuf->HasChainedBuffer())
    {
        msgBuf->CompactHead();

        if (msgBuf->HasChainedBuffer())
        {
            ret = CHIP_ERROR_OUTBOUND_MESSAGE_TOO_BIG;
            ChipLogError(DeviceLayer, "WiFi-PAF: Outbound message too big (%lu), skip temporally", msgBuf->DataLength());
            return ret;
        }
    }

    wifi_nan_followup_params_t msgParams;
    memset(&msgParams, 0, sizeof(msgParams));

    msgParams.inst_id = mNanPublishId;
    msgParams.peer_inst_id mNanPeerInstanceId;
    msgParams.protocol = PROTOCOL_CSA_MATTER;
    msgParams.ssi_len = msgBuf->DataLength();
    msgParams.ssi = msgBuf->Start();

    esp_err_t err = esp_wifi_nan_send_message(&msgParams);
    VerifyOrReturnError(err == ESP_OK, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "esp_wifi_nan_send_message failed, esp_err:%d", err));

    ChipLogProgress(DeviceLayer, "done sending WiFi-PAF");

    return CHIP_NO_ERROR;
}

Transport::WiFiPAFBase * ConnectivityManagerImpl::_GetWiFiPAF()
{
    return pmWiFiPAF;
}

void ConnectivityManagerImpl::_SetWiFiPAF(Transport::WiFiPAFBase * pWiFiPAF)
{
    pmWiFiPAF = pWiFiPAF;
    return;
}
