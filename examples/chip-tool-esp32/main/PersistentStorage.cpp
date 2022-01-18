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
#include <sstream>
#include <PersistentStorage.h>
#include <lib/core/CHIPEncoding.h>
#include <protocols/secure_channel/PASESession.h>
#include <platform/esp32/ESP32Config.h>

using namespace ::chip;
using namespace ::chip::Controller;
using namespace ::chip::Logging;
using namespace ::chip::DeviceLayer::Internal;

constexpr const char kDefaultSectionName[] = "Default";
constexpr const char kPortKey[]            = "ListenPort";
constexpr const char kLoggingKey[]         = "LoggingLevel";
constexpr const char kLocalNodeIdKey[]     = "LocalNodeId";
constexpr LogCategory kDefaultLoggingLevel = kLogCategory_Detail;

CHIP_ERROR PersistentStorage::Init(const char * name)
{
//    ReturnErrorOnFailure(ESP32Config::EnsureNamespace(name));
    mName = name;
    return CHIP_NO_ERROR;
}

CHIP_ERROR PersistentStorage::SyncGetKeyValue(const char * key, void * value, uint16_t & size)
{
    struct ESP32Config::Key readKey;
    readKey.Namespace = mName;
    readKey.Name      = key;

    size_t outSize = size;
    ReturnErrorOnFailure(ESP32Config::ReadConfigValueBin(readKey, static_cast<uint8_t *>(value), size, outSize));
    size = outSize;
    return CHIP_NO_ERROR;
}

CHIP_ERROR PersistentStorage::SyncSetKeyValue(const char * key, const void * value, uint16_t size)
{
    struct ESP32Config::Key writeKey;
    writeKey.Namespace = mName;
    writeKey.Name      = key;

    ReturnErrorOnFailure(ESP32Config::WriteConfigValueBin(writeKey, static_cast<const uint8_t *>(value), size));
    return CHIP_NO_ERROR;
}

CHIP_ERROR PersistentStorage::SyncDeleteKeyValue(const char * key)
{
    struct ESP32Config::Key deleteKey;
    deleteKey.Namespace = mName;
    deleteKey.Name      = key;

    ReturnErrorOnFailure(ESP32Config::ClearConfigValue(deleteKey));
    return CHIP_NO_ERROR;
}

uint16_t PersistentStorage::GetListenPort()
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    // By default chip-tool listens on CHIP_PORT + 1. This is done in order to avoid
    // having 2 servers listening on CHIP_PORT when one runs an accessory server locally.
    uint16_t chipListenPort = static_cast<uint16_t>(CHIP_PORT + 1);

    char value[6];
    uint16_t size = static_cast<uint16_t>(sizeof(value));
    err           = SyncGetKeyValue(kPortKey, value, size);
    if (CHIP_NO_ERROR == err)
    {
        uint16_t tmpValue;
        std::stringstream ss(value);
        ss >> tmpValue;
        if (!ss.fail() && ss.eof())
        {
            chipListenPort = tmpValue;
        }
    }

    return chipListenPort;
}

LogCategory PersistentStorage::GetLoggingLevel()
{
    CHIP_ERROR err           = CHIP_NO_ERROR;
    LogCategory chipLogLevel = kDefaultLoggingLevel;

    char value[9];
    uint16_t size = static_cast<uint16_t>(sizeof(value));
    err           = SyncGetKeyValue(kLoggingKey, value, size);
    if (CHIP_NO_ERROR == err)
    {
        if (strcasecmp(value, "none") == 0)
        {
            chipLogLevel = kLogCategory_None;
        }
        else if (strcasecmp(value, "error") == 0)
        {
            chipLogLevel = kLogCategory_Error;
        }
        else if (strcasecmp(value, "progress") == 0)
        {
            chipLogLevel = kLogCategory_Progress;
        }
        else if (strcasecmp(value, "detail") == 0)
        {
            chipLogLevel = kLogCategory_Detail;
        }
    }

    return chipLogLevel;
}

NodeId PersistentStorage::GetLocalNodeId()
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    uint64_t nodeId;
    uint16_t size = static_cast<uint16_t>(sizeof(nodeId));
    err           = SyncGetKeyValue(kLocalNodeIdKey, &nodeId, size);
    if (err == CHIP_NO_ERROR)
    {
        return static_cast<NodeId>(Encoding::LittleEndian::HostSwap64(nodeId));
    }

    return kTestControllerNodeId;
}

CHIP_ERROR PersistentStorage::SetLocalNodeId(NodeId value)
{
    uint64_t nodeId = Encoding::LittleEndian::HostSwap64(value);
    return SyncSetKeyValue(kLocalNodeIdKey, &nodeId, sizeof(nodeId));
}
