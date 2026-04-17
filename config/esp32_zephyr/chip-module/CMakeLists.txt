#
#   Copyright (c) 2024 Project CHIP Authors
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#

#
#   @file
#     CMake sub-project defining 'chip' target which represents CHIP library
#     for ESP32 Zephyr platform. Uses ExternalProject to build with GN.
#     It is assumed that find_package(Zephyr) has been called before
#     including this file.
#

if (CONFIG_CHIP)

include(ExternalProject)
include(../../zephyr/ota-image.cmake)
include(../../zephyr/zephyr-util.cmake)

# ==============================================================================
# Prepare CHIP configuration based on the project Kconfig configuration
# ==============================================================================

if (NOT CHIP_ROOT)
    get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. REALPATH)
endif()
get_filename_component(GN_ROOT_TARGET ${CHIP_ROOT}/config/esp32_zephyr/chip-gn REALPATH)
get_filename_component(COMMON_CMAKE_SOURCE_DIR ${CHIP_ROOT}/config/common/cmake REALPATH)

# Get common Cmake sources

include(${COMMON_CMAKE_SOURCE_DIR}/chip_gn_args.cmake)
include(${COMMON_CMAKE_SOURCE_DIR}/chip_gn.cmake)

# Prepare compiler flags
if (CHIP_CFLAGS)
    matter_add_flags("${CHIP_CFLAGS}")
endif()

matter_add_flags(-D_POSIX_C_SOURCE=200809)
# WiFiManager.cpp uses assert() — force-include for GN build
list(APPEND MATTER_CFLAGS -include)
list(APPEND MATTER_CFLAGS assert.h)

if(CONFIG_MBEDTLS)
    zephyr_include_directories($<TARGET_PROPERTY:mbedTLS,INTERFACE_INCLUDE_DIRECTORIES>)
    zephyr_compile_definitions($<TARGET_PROPERTY:mbedTLS,INTERFACE_COMPILE_DEFINITIONS>)
endif()

if (CONFIG_POSIX_API)
    matter_add_flags(-D_DEFAULT_SOURCE)
    matter_add_flags(-isystem${ZEPHYR_BASE}/include/zephyr/posix)
endif()

# Zephyr's newlib config defines _ANSI_SOURCE which hides POSIX types (clock_t, pid_t)
# needed by CHIP. Remove it from Zephyr's compile definitions so GN build gets POSIX types.
get_target_property(_zephyr_defs zephyr_interface INTERFACE_COMPILE_DEFINITIONS)
if(_zephyr_defs)
    list(REMOVE_ITEM _zephyr_defs "_ANSI_SOURCE")
    set_property(TARGET zephyr_interface PROPERTY INTERFACE_COMPILE_DEFINITIONS ${_zephyr_defs})
endif()

matter_add_flags(-DNL_ASSERT_USE_FLAGS_DEFAULT=1)

zephyr_include_directories(
    ${CHIP_ROOT}/src/platform/esp32_zephyr
)

zephyr_get_compile_flags(ZEPHYR_CFLAGS_C C)
# Strip _ANSI_SOURCE from Newlib flags — it prevents POSIX types (clock_t, pid_t)
# that CHIP needs. We define _DEFAULT_SOURCE and _POSIX_C_SOURCE instead.
string(REPLACE "-D_ANSI_SOURCE" "" ZEPHYR_CFLAGS_C "${ZEPHYR_CFLAGS_C}")
# Strip atomic extension from -march to match linker (Zephyr links without libatomic)
string(REGEX REPLACE "-march=rv32imac" "-march=rv32imc" ZEPHYR_CFLAGS_C "${ZEPHYR_CFLAGS_C}")
matter_add_cflags("${ZEPHYR_CFLAGS_C}")
zephyr_get_compile_flags(ZEPHYR_CFLAGS_CC CXX)
string(REPLACE "-D_ANSI_SOURCE" "" ZEPHYR_CFLAGS_CC "${ZEPHYR_CFLAGS_CC}")
string(REGEX REPLACE "-march=rv32imac" "-march=rv32imc" ZEPHYR_CFLAGS_CC "${ZEPHYR_CFLAGS_CC}")
matter_add_cxxflags("${ZEPHYR_CFLAGS_CC}")
zephyr_get_gnu_cpp_standard(ZEPHYR_GNU_CPP_STD)
matter_add_cxxflags(${ZEPHYR_GNU_CPP_STD})

# Set up custom OpenThread configuration

if (CONFIG_CHIP_OPENTHREAD_CONFIG)
    get_filename_component(CHIP_OPENTHREAD_CONFIG
        ${CONFIG_CHIP_OPENTHREAD_CONFIG}
        REALPATH
        BASE_DIR ${CMAKE_SOURCE_DIR}
    )
    zephyr_set_openthread_config(${CHIP_OPENTHREAD_CONFIG})
endif()

# ==============================================================================
# Generate configuration for CHIP GN build system
# ==============================================================================

matter_common_gn_args(
    DEBUG           CONFIG_DEBUG
    LIB_SHELL       CONFIG_CHIP_LIB_SHELL
    LIB_TESTS       CONFIG_CHIP_BUILD_TESTS
    PROJECT_CONFIG  ${CONFIG_CHIP_PROJECT_CONFIG}
    DEVICE_INFO_EXAMPLE_PROVIDER CONFIG_CHIP_EXAMPLE_DEVICE_INFO_PROVIDER
)
matter_add_gn_arg_string("zephyr_ar"                              ${CMAKE_AR})
matter_add_gn_arg_string("zephyr_cc"                              ${CMAKE_C_COMPILER})
matter_add_gn_arg_string("zephyr_cxx"                             ${CMAKE_CXX_COMPILER})
matter_add_gn_arg_bool  ("chip_logging"                           CONFIG_LOG)
matter_add_gn_arg_bool  ("chip_enable_openthread"                 CONFIG_NET_L2_OPENTHREAD)
matter_add_gn_arg_bool  ("chip_openthread_ftd"                    CONFIG_OPENTHREAD_FTD)
matter_add_gn_arg_bool  ("chip_config_network_layer_ble"          CONFIG_BT)
matter_add_gn_arg_bool  ("chip_inet_config_enable_ipv4"           CONFIG_NET_IPV4)
matter_add_gn_arg_bool  ("chip_enable_ota_requestor"              CONFIG_CHIP_OTA_REQUESTOR)
matter_add_gn_arg_bool  ("chip_inet_config_enable_tcp_endpoint"   CONFIG_CHIP_BUILD_TESTS)
matter_add_gn_arg_bool  ("chip_error_logging"                     CONFIG_MATTER_LOG_LEVEL GREATER_EQUAL 1)
matter_add_gn_arg_bool  ("chip_progress_logging"                  CONFIG_MATTER_LOG_LEVEL GREATER_EQUAL 3)
matter_add_gn_arg_bool  ("chip_detail_logging"                    CONFIG_MATTER_LOG_LEVEL GREATER_EQUAL 4)
matter_add_gn_arg_bool  ("chip_automation_logging"                FALSE)
matter_add_gn_arg_bool  ("chip_malloc_sys_heap"                   CONFIG_CHIP_MALLOC_SYS_HEAP)
matter_add_gn_arg_bool  ("chip_enable_icd_server"                 CONFIG_CHIP_ENABLE_ICD_SUPPORT)
matter_add_gn_arg_bool  ("chip_enable_wifi"                       CONFIG_WIFI_ESP32)

if (CONFIG_CHIP_ENABLE_ICD_SUPPORT)
    matter_add_gn_arg_bool  ("chip_enable_icd_lit"                      CONFIG_CHIP_ICD_LIT_SUPPORT)
    matter_add_gn_arg_bool  ("chip_enable_icd_checkin"                  CONFIG_CHIP_ICD_CHECK_IN_SUPPORT)
    matter_add_gn_arg_bool  ("chip_enable_icd_user_active_mode_trigger" CONFIG_CHIP_ICD_UAT_SUPPORT)
endif()

if (CONFIG_CHIP_FACTORY_DATA)
    matter_add_gn_arg_bool  ("chip_use_transitional_commissionable_data_provider"  "false")
    matter_add_gn_arg_bool  ("chip_enable_factory_data"                            "true")
elseif (CONFIG_CHIP_FACTORY_DATA_CUSTOM_BACKEND)
    matter_add_gn_arg_bool  ("chip_use_transitional_commissionable_data_provider"  "false")
endif()

if (CONFIG_CHIP_ROTATING_DEVICE_ID)
    matter_add_gn_arg_bool("chip_enable_rotating_device_id"          "true")
    matter_add_gn_arg_bool("chip_enable_additional_data_advertising" "true")
endif()

if (CONFIG_CHIP_ENABLE_DNSSD_SRP)
    matter_add_gn_arg_string("chip_mdns" "platform")
endif()

if (CONFIG_CHIP_PW_RPC)
    set(PIGWEED_DIR "//third_party/pigweed/repo")
    matter_add_gn_arg_string("pw_assert_BACKEND" ${PIGWEED_DIR}/pw_assert_log:check_backend)
    matter_add_gn_arg_string("pw_log_BACKEND" ${PIGWEED_DIR}/pw_log_basic)
    matter_add_gn_arg("pw_build_LINK_DEPS" [\"${PIGWEED_DIR}/pw_assert:impl\",\ \"${PIGWEED_DIR}/pw_log:impl\"])
endif()

matter_generate_args_tmp_file()

# ==============================================================================
# Build chip library
# ==============================================================================
matter_build(chip
    LIB_SHELL   ${CONFIG_CHIP_LIB_SHELL}
    DEVICE_INFO_EXAMPLE_PROVIDER ${CONFIG_CHIP_EXAMPLE_DEVICE_INFO_PROVIDER}
    GN_DEPENDENCIES kernel
)
set_property(GLOBAL APPEND PROPERTY ZEPHYR_INTERFACE_LIBS chip)

# Link kernel symbols referenced by Matter
target_link_libraries(chip INTERFACE $<TARGET_FILE:kernel>)

# Link mbedTLS libraries needed by CHIP crypto (CCM, ECP, HKDF, etc.)
# Must come after libCHIP.a to resolve symbols
target_link_libraries(chip INTERFACE
    $<TARGET_FILE:mbedTLSCrypto>
    $<TARGET_FILE:mbedTLSBase>
    $<TARGET_FILE:mbedTLSX509>
)

# RISC-V without A extension needs GCC's atomic builtins from libgcc
target_link_libraries(chip INTERFACE -lgcc)

if (CONFIG_CHIP_MALLOC_SYS_HEAP_OVERRIDE)
    target_link_options(chip INTERFACE
        -Wl,--wrap=malloc
        -Wl,--wrap=calloc
        -Wl,--wrap=realloc
        -Wl,--wrap=free
        -Wl,--wrap=_malloc_r
        -Wl,--wrap=_calloc_r
        -Wl,--wrap=_realloc_r
        -Wl,--wrap=_free_r
    )
endif()

endif() # CONFIG_CHIP
