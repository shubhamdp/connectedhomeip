/*
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

#pragma once

// Reduce verbose logging
#define CHIP_CONFIG_LOG_MODULE_Zcl_PROGRESS 0
#define CHIP_CONFIG_LOG_MODULE_InteractionModel_PROGRESS 0
#define CHIP_CONFIG_LOG_MODULE_InteractionModel_DETAIL 0
#define CHIP_CONFIG_LOG_MODULE_DataManagement_PROGRESS 0
#define CHIP_CONFIG_LOG_MODULE_FabricProvisioning_PROGRESS 0
#define CHIP_CONFIG_LOG_MODULE_SecureChannel_PROGRESS 0

// Reduce event buffer sizes to save DRAM
#define CHIP_CONFIG_EVENT_LOGGING_CRIT_BUFFER_SIZE 256
#define CHIP_CONFIG_EVENT_LOGGING_INFO_BUFFER_SIZE 256
#define CHIP_CONFIG_EVENT_LOGGING_DEBUG_BUFFER_SIZE 256

// Reduce exchange contexts and handlers
#define CHIP_CONFIG_MAX_EXCHANGE_CONTEXTS 4
#define CHIP_CONFIG_MAX_UNSOLICITED_MESSAGE_HANDLERS 4

// Reduce fabric count
#define CHIP_CONFIG_MAX_FABRICS 3

// Reduce CASE sessions
#define CHIP_CONFIG_DEVICE_MAX_ACTIVE_CASE_CLIENTS 1
#define CHIP_CONFIG_DEVICE_MAX_ACTIVE_DEVICES 1

// Reduce packet buffer pool
#define CHIP_SYSTEM_CONFIG_PACKETBUFFER_POOL_SIZE 4
