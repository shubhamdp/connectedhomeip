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
#include <DevicePairingCommands.h>
#include <ExampleCredentialIssuerCommands.h>
#include <platform/ESP32/ESP32PersistentStorage.h>

class ESP32Controller
{
public:
    static ESP32Controller & GetInstance()
    {
        static ESP32Controller instance;
        return instance;
    }

    CHIP_ERROR Init();

    chip::Controller::DeviceCommissioner & GetCommissioner(void) { return mCommissioner; }

protected:
    chip::SimpleFabricStorage mFabricStorage;
    ESP32PersistentStorage mDefaultStorage;
    ESP32PersistentStorage mCommissionerStorage;

    ExampleCredentialIssuerCommands mExampleCredentialIssuerCmds;
    CredentialIssuerCommands * mCredIssuerCmds = &mExampleCredentialIssuerCmds;

private:
    CHIP_ERROR InitializeCommissioner(void);

    CHIP_ERROR ShutdownCommissioner(void);

    chip::Controller::DeviceCommissioner mCommissioner;
    DevicePairingCommands mDevicePairingCommands;
};
