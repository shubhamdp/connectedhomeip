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
 *          Platform-specific configuration overrides for the chip Device Layer
 *          on ESP32 Zephyr platform.
 */

#pragma once

// ==================== Platform Adaptations ====================

#ifdef CONFIG_WIFI_ESP32
#define CHIP_DEVICE_CONFIG_ENABLE_WIFI 1
#else
#ifndef CHIP_DEVICE_CONFIG_ENABLE_WIFI
#define CHIP_DEVICE_CONFIG_ENABLE_WIFI 0
#endif
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_WIFI
#define CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION 1
#define CHIP_DEVICE_CONFIG_ENABLE_WIFI_AP 0
#else
#define CHIP_DEVICE_CONFIG_ENABLE_WIFI_STATION 0
#define CHIP_DEVICE_CONFIG_ENABLE_WIFI_AP 0
#endif

#ifndef CHIP_DEVICE_CONFIG_ENABLE_ETHERNET
#define CHIP_DEVICE_CONFIG_ENABLE_ETHERNET 0
#endif // CHIP_DEVICE_CONFIG_ENABLE_ETHERNET

#ifdef CONFIG_BT
#define CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE CONFIG_BT
#else
#define CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE 0
#endif

#ifdef CONFIG_NET_L2_OPENTHREAD
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD CONFIG_NET_L2_OPENTHREAD
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT 1
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD_DNS_CLIENT 1
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD_COMMISSIONABLE_DISCOVERY 1
#define OPENTHREAD_CONFIG_IP6_SLAAC_ENABLE 1
#else
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD 0
#endif

// ========== Platform-specific Configuration =========

#ifndef CHIP_DEVICE_CONFIG_SETTINGS_KEY
#define CHIP_DEVICE_CONFIG_SETTINGS_KEY "mt"
#endif

#ifndef CHIP_DEVICE_CONFIG_HEAP_STATISTICS_MALLINFO
#if !defined(CONFIG_CHIP_MALLOC_SYS_HEAP) && defined(CONFIG_NEWLIB_LIBC)
#define CHIP_DEVICE_CONFIG_HEAP_STATISTICS_MALLINFO 1
#else
#define CHIP_DEVICE_CONFIG_HEAP_STATISTICS_MALLINFO 0
#endif
#endif // CHIP_DEVICE_CONFIG_HEAP_STATISTICS_MALLINFO

#ifndef CHIP_DEVICE_BLE_ADVERTISING_PRIORITY
#define CHIP_DEVICE_BLE_ADVERTISING_PRIORITY 0
#endif

// ========== Platform-specific Configuration Overrides =========

#ifndef CHIP_DEVICE_CONFIG_CHIP_TASK_PRIORITY
#define CHIP_DEVICE_CONFIG_CHIP_TASK_PRIORITY (K_PRIO_PREEMPT(1))
#endif

#ifndef CHIP_DEVICE_CONFIG_CHIP_TASK_STACK_SIZE
#define CHIP_DEVICE_CONFIG_CHIP_TASK_STACK_SIZE (8 * 1024)
#endif

#ifndef CHIP_DEVICE_CONFIG_THREAD_TASK_STACK_SIZE
#define CHIP_DEVICE_CONFIG_THREAD_TASK_STACK_SIZE (7 * 1024)
#endif

#define CHIP_DEVICE_CONFIG_MAX_EVENT_QUEUE_SIZE 25

#define CHIP_DEVICE_CONFIG_ENABLE_WIFI_TELEMETRY 0
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD_TELEMETRY 0
#define CHIP_DEVICE_CONFIG_ENABLE_THREAD_TELEMETRY_FULL 0

#define CHIP_DEVICE_CONFIG_CHIPOBLE_ENABLE_ADVERTISING_AUTOSTART 0

#ifndef CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART
#define CHIP_DEVICE_CONFIG_ENABLE_PAIRING_AUTOSTART 0
#else
#define CHIP_DEVICE_CONFIG_ENABLE_PAIRING_AUTOSTART CONFIG_CHIP_ENABLE_PAIRING_AUTOSTART
#endif
