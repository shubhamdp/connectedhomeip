/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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

#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>
#include <DevicePairingCommands.h>
#include <controller/CommissioningDelegate.h>
#include <controller/CHIPDeviceController.h>
#include <lib/support/Span.h>

void DevicePairingCommands::OnStatusUpdate(chip::Controller::DevicePairingDelegate::Status status)
{
//    if (status == chip::Controller::DevicePairingDelegate::Status::SecurePairingSuccess)
//    {
//        ChipLogProgress(Controller, "Secure pairing succeeded");
//    }
//    else
//    {
//        ChipLogProgress(Controller, "Secure pairing failed");
//    }
}

void DevicePairingCommands::OnPairingComplete(CHIP_ERROR error)
{
//    if (error == CHIP_NO_ERROR)
//    {
//        ChipLogProgress(Controller, "Pairing succeeded");
//    }
//    else
//    {
//        ChipLogProgress(Controller, "Pairing failed with error: %" CHIP_ERROR_FORMAT, error.Format());
//    }
}

void DevicePairingCommands::OnPairingDeleted(CHIP_ERROR error)
{
//     if (error == CHIP_NO_ERROR)
//     {
//         ChipLogProgress(Controller, "Pairing deleted");
//     }
//     else
//     {
//         ChipLogProgress(Controller, "Pairing delete failed with error: %" CHIP_ERROR_FORMAT, error.Format());
//     }
}

void DevicePairingCommands::OnCommissioningComplete(chip::NodeId deviceId, CHIP_ERROR error)
{
//    if (error == CHIP_NO_ERROR)
//    {
//        ChipLogProgress(Controller, "Commissioning succeeded NodeId 0x" ChipLogFormatX64, ChipLogValueX64(deviceId));
//    }
//    else
//    {
//        ChipLogProgress(Controller, "Commissioning failed NodeId 0x" ChipLogFormatX64 " error: %" CHIP_ERROR_FORMAT, ChipLogValueX64(deviceId), error.Format());
//    }
}

// void DevicePairingCommands::OnDiscoveredDevice(const chip::Dnssd::DiscoveredNodeData & nodeData)
// {
//    const uint16_t port = nodeData.port;
//    char buf[chip::Inet::IPAddress::kMaxStringLength];
//    nodeData.ipAddress[0].ToString(buf);
//    ChipLogProgress(chipTool, "Discovered Device: %s:%u", buf, port);
//
//    // TODO: Figure out how to deal with onnetwork mdns pairing
//    // Stop Mdns discovery. Is it the right method ?
//    CurrentCommissioner().RegisterDeviceDiscoveryDelegate(nullptr);

//     Inet::InterfaceId interfaceId = nodeData.ipAddress[0].IsIPv6LinkLocal() ? nodeData.interfaceId[0] : Inet::InterfaceId::Null();
//     PeerAddress peerAddress       = PeerAddress::UDP(nodeData.ipAddress[0], port, interfaceId);
//     CHIP_ERROR err                = Pair(mNodeId, peerAddress);
//     if (CHIP_NO_ERROR != err)
//     {
//         SetCommandExitStatus(err);
//     }
// }

void DevicePairingCommands::PairBleWifi(chip::NodeId nodeId, uint32_t setupPasscode, uint16_t discriminator, const char * ssid, const char * passphrase)
{
    chip::ByteSpan ssidSpan(reinterpret_cast<const uint8_t *>(ssid), strlen(ssid));
    chip::ByteSpan passphraseSpan(reinterpret_cast<const uint8_t *>(passphrase), strlen(passphrase));

    chip::Controller::CommissioningParameters cParams = chip::Controller::CommissioningParameters().SetWiFiCredentials(chip::Controller::WiFiCredentials(ssidSpan, passphraseSpan));

    chip::RendezvousParameters rParams = chip::RendezvousParameters().SetSetupPINCode(setupPasscode).SetDiscriminator(discriminator).SetPeerAddress(chip::Transport::PeerAddress::BLE());

    mDeviceCommissioner->PairDevice(nodeId, rParams, cParams);
}
