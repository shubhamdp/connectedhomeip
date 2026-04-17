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

/**
 *    @file
 *          Platform-specific configuration overrides for CHIP on
 *          ESP32 Zephyr platform.
 */

#pragma once

// ==================== General Platform Adaptations ====================

#define CHIP_CONFIG_ABORT() abort()

#define CHIP_CONFIG_PERSISTED_STORAGE_KEY_TYPE const char *
#define CHIP_CONFIG_PERSISTED_STORAGE_MAX_KEY_LENGTH 2

#define CHIP_CONFIG_LIFETIIME_PERSISTED_COUNTER_KEY "rc"

// ==================== Security Adaptations ====================

// SHA256 context size for mbedTLS software implementation
#define CHIP_CONFIG_SHA256_CONTEXT_SIZE 208

// ==================== General Configuration Overrides ====================

#ifndef CHIP_CONFIG_MAX_UNSOLICITED_MESSAGE_HANDLERS
#define CHIP_CONFIG_MAX_UNSOLICITED_MESSAGE_HANDLERS 8
#endif

#ifndef CHIP_CONFIG_MAX_EXCHANGE_CONTEXTS
#define CHIP_CONFIG_MAX_EXCHANGE_CONTEXTS 8
#endif

#ifndef CHIP_LOG_FILTERING
#define CHIP_LOG_FILTERING 0
#endif

#ifndef CHIP_CONFIG_BDX_MAX_NUM_TRANSFERS
#define CHIP_CONFIG_BDX_MAX_NUM_TRANSFERS 1
#endif

#ifndef CHIP_CONFIG_MAX_FABRICS
#define CHIP_CONFIG_MAX_FABRICS 5
#endif

// Set MRP retry intervals for Thread to test-proven values.
#ifndef CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL
#ifdef CONFIG_NET_L2_OPENTHREAD
#define CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL (800_ms32)
#else
#define CHIP_CONFIG_MRP_LOCAL_ACTIVE_RETRY_INTERVAL (1000_ms32)
#endif
#endif

#ifndef CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL
#ifdef CONFIG_NET_L2_OPENTHREAD
#define CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL (800_ms32)
#else
#define CHIP_CONFIG_MRP_LOCAL_IDLE_RETRY_INTERVAL (1000_ms32)
#endif
#endif
