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

#include <platform/ESP32/ConnectivityManagerImpl.h>
#include <platform/ConnectivityManager.h>
#include <platform/CommissionableDataProvider.h>

#include <lib/support/CHIPMemString.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/DeviceInstanceInfoProvider.h>

#include "esp_wifi.h"
#include "esp_nan.h"
#include "esp_event.h"
#include "esp_mac.h"

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

namespace chip {
namespace DeviceLayer {

CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFPublish(ConnectivityManager::WiFiPAFAdvertiseParam & InArgs)
{
    ChipLogError(DeviceLayer, "WiFi-PAF: Publish");
    struct PAFPublishSSI PafPublish_ssi;

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
    VerifyOrReturnError(err == ESP_OK, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "esp_wifi_nan_start failed, esp_err:%d", err));

    // TODO: Some parameters should be configurable somehow
    wifi_nan_publish_cfg_t publishConfig;
    memset(&publishConfig, 0, sizeof(publishConfig));

    Platform::CopyString(publishConfig.service_name, SERVICE_NAME);
    publishConfig.type = static_cast<wifi_nan_service_type_t>(NAN_PUBLISH_UNSOLICITED | NAN_PUBLISH_SOLICITED);
    publishConfig.srv_proto_type = PROTOCOL_CSA_MATTER;

    uint8_t MATTER_SERVICE_DATA[] = { 0x00, 0x00, 0x0F, 0x00, 0x01, 0x80, 0xF1, 0xFF };
    publishConfig.ssi = MATTER_SERVICE_DATA;
    publishConfig.ssi_len = sizeof(MATTER_SERVICE_DATA);

    /*publishConfig.ssi = reinterpret_cast<uint8_t *>(&PafPublish_ssi);*/
    /*publishConfig.ssi_len = sizeof(PafPublish_ssi);*/
    publishConfig.ttl = 300;

    uint8_t chan_list[] = {1,6,11};
    publishConfig.usd_chan_list = chan_list;
    publishConfig.usd_chan_list_len = sizeof(chan_list) / sizeof(chan_list[0]);

    printf("\nssi:");
    for (int i = 0; i < publishConfig.ssi_len; i++)
    {
        printf("%02X", publishConfig.ssi[i]);
    }
    printf("\n");

    printf("service name -- %s\n", publishConfig.service_name);
    printf("srv_proto_type -- %d\n", publishConfig.srv_proto_type);
    printf("ttl -- %d\n", publishConfig.ttl);

    mNanPublishId = esp_wifi_nan_publish_service(&publishConfig, false /* ndp_resp_needed */);
    VerifyOrReturnError(mNanPublishId != 0, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "esp_wifi_nan_publish_service failed"));

    ChipLogProgress(DeviceLayer, "WiFi-PAF: Publish Done, id: %u", mNanPublishId);

    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_WiFiPAFCancelPublish()
{
    ChipLogError(DeviceLayer, "WiFi-PAF: Cancel Publish");

    esp_err_t err = esp_wifi_nan_cancel_publish(mNanPublishId);
    VerifyOrReturnError(err == ESP_OK, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "esp_wifi_nan_cancel_publish failed"));
    return CHIP_NO_ERROR;
}

CHIP_ERROR ConnectivityManagerImpl::_SetWiFiPAFAdvertisingEnabled(WiFiPAFAdvertiseParam & args)
{
    ChipLogProgress(DeviceLayer, "WiFi-PAF: %s", args.enable ? "Enable" : "Disable");
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

void ConnectivityManagerImpl::OnNanReceive(const wifi_event_nan_receive_t * eventData)
{
    ChipLogProgress(DeviceLayer, "Our service identifier: %u", eventData->inst_id);
    ChipLogProgress(DeviceLayer, "Peer service identifier: %u", eventData->peer_inst_id);
    ChipLogProgress(DeviceLayer, "peer mac " MACSTR, MAC2STR(eventData->peer_if_mac));
    ChipLogProgress(DeviceLayer, "ssi len: %u", eventData->ssi_len);

    VerifyOrReturn(eventData->ssi_len > 0, ChipLogError(DeviceLayer, "SSI length is zero"));

    mNanPeerInstanceId = eventData->peer_inst_id;

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
    ChipLogProgress(DeviceLayer, "WiFi-PAF: Sending %u bytes", msgBuf->DataLength());

    CHIP_ERROR ret = CHIP_NO_ERROR;

    if (msgBuf.IsNull())
    {
        ChipLogError(DeviceLayer, "WiFi-PAF: Invalid Packet (%u)", msgBuf->DataLength());
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
            ChipLogError(DeviceLayer, "WiFi-PAF: Outbound message too big (%u), skip temporally", msgBuf->DataLength());
            return ret;
        }
    }

    wifi_nan_followup_params_t msgParams;
    memset(&msgParams, 0, sizeof(msgParams));

    msgParams.inst_id = mNanPublishId;
    msgParams.peer_inst_id  = mNanPeerInstanceId;
    msgParams.protocol = PROTOCOL_CSA_MATTER;
    msgParams.ssi_len = static_cast<uint16_t>(msgBuf->DataLength());
    msgParams.ssi = msgBuf->Start();

    esp_err_t err = esp_wifi_nan_send_message(&msgParams);
    VerifyOrReturnError(err == ESP_OK, CHIP_ERROR_INTERNAL, ChipLogError(DeviceLayer, "esp_wifi_nan_send_message failed, esp_err:%d", err));

    ChipLogProgress(DeviceLayer, "done sending WiFi-PAF");

    return CHIP_NO_ERROR;
}

} // namespace DeviceLayer
} // namespace chip
