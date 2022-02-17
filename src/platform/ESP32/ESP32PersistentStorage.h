/*
 *   Copyright (c) 2022 Project CHIP Authors
 *   All rights reserved.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */
#pragma once
#include <lib/core/CHIPPersistentStorageDelegate.h>
#include <platform/ESP32/ESP32Config.h>

class ESP32PersistentStorage : public chip::PersistentStorageDelegate
{
public:
    CHIP_ERROR Init(const char *name)
    {
        mName = name;
        return CHIP_NO_ERROR;
    }

    /////////// PersistentStorageDelegate Interface /////////
    CHIP_ERROR SyncGetKeyValue(const char * key, void * buffer, uint16_t & size) override
    {
        struct chip::DeviceLayer::Internal::ESP32Config::Key readKey = { mName, key };
        size_t outSize = size;
        ReturnErrorOnFailure(chip::DeviceLayer::Internal::ESP32Config::ReadConfigValueBin(readKey, static_cast<uint8_t *>(buffer), size, outSize));
        size = outSize;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR SyncSetKeyValue(const char * key, const void * value, uint16_t size) override
    {
        struct chip::DeviceLayer::Internal::ESP32Config::Key writeKey = { mName, key };
        ReturnErrorOnFailure(chip::DeviceLayer::Internal::ESP32Config::WriteConfigValueBin(writeKey, static_cast<const uint8_t *>(value), size));
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR SyncDeleteKeyValue(const char * key) override
    {
        struct chip::DeviceLayer::Internal::ESP32Config::Key deleteKey = { mName, key };
        ReturnErrorOnFailure(chip::DeviceLayer::Internal::ESP32Config::ClearConfigValue(deleteKey));
        return CHIP_NO_ERROR;
    }

private:
    const char * mName = nullptr;
};
