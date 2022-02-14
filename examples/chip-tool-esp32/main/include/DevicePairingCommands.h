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
#pragma once
#include <controller/CHIPDeviceController.h>
// #include <controller/DeviceDiscoveryDelegate.h>

class DevicePairingCommands : public chip::Controller::DevicePairingDelegate,
  //                            public chip::Controller::DeviceDiscoveryDelegate
{
public:
    static DevicePairingCommands & GetInstance()
    {
        static DevicePairingCommands instance;
        return instance;
    }

    void SetDeviceCommissioner(chip::Controller::DeviceCommissioner * pDeviceCommissioner)
    {
        mDeviceCommissioner = pDeviceCommissioner;
    }

    void PairBleWifi(chip::NodeId nodeId, uint32_t setupPasscode, uint16_t discriminator, const char * ssid, const char * passphrase);

    ///// DevicePairingDelegate /////
    void OnStatusUpdate(DevicePairingDelegate::Status status) override;

    void OnPairingComplete(CHIP_ERROR error) override;

    void OnPairingDeleted(CHIP_ERROR error) override;

    void OnCommissioningComplete(chip::NodeId deviceId, CHIP_ERROR error) override;

    ////// DeviceDiscoveryDelegate //////
//    void OnDiscoveredDevice(const chip::Dnssd::DiscoveredNodeData & nodeData) override;

private:
    chip::Controller::DeviceCommissioner * mDeviceCommissioner = nullptr;
};
