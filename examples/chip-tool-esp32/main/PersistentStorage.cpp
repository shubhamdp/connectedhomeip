/*
 *   Copyright (c) 2021 Project CHIP Authors
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
#include <PersistentStorage.h>
#include <platform/esp32/ESP32Config.h>

CHIP_ERROR PersistentStorage::Init(const char * name)
{
//    ReturnErrorOnFailure(ESP32Config::EnsureNamespace(name));
    mName = name;
    return CHIP_NO_ERROR;
}

CHIP_ERROR PersistentStorage::SyncGetKeyValue(const char * key, void * value, uint16_t & size)
{
    struct chip::DeviceLayer::Internal::ESP32Config::Key readKey = { mName, key };
    size_t outSize = size;
    ReturnErrorOnFailure(chip::DeviceLayer::Internal::ESP32Config::ReadConfigValueBin(readKey, static_cast<uint8_t *>(value), size, outSize));
    size = outSize;
    return CHIP_NO_ERROR;
}

CHIP_ERROR PersistentStorage::SyncSetKeyValue(const char * key, const void * value, uint16_t size)
{
    struct chip::DeviceLayer::Internal::ESP32Config::Key writeKey = { mName, key };
    ReturnErrorOnFailure(chip::DeviceLayer::Internal::ESP32Config::WriteConfigValueBin(writeKey, static_cast<const uint8_t *>(value), size));
    return CHIP_NO_ERROR;
}

CHIP_ERROR PersistentStorage::SyncDeleteKeyValue(const char * key)
{
    struct chip::DeviceLayer::Internal::ESP32Config::Key deleteKey = { mName, key };
    ReturnErrorOnFailure(chip::DeviceLayer::Internal::ESP32Config::ClearConfigValue(deleteKey));
    return CHIP_NO_ERROR;
}
