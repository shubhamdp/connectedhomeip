#
#    Copyright (c) 2024 Project CHIP Authors
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#
cmake_minimum_required(VERSION 3.13.1)

get_filename_component(CHIP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/connectedhomeip REALPATH)
get_filename_component(LIGHTING_COMMON ${CHIP_ROOT}/examples/lighting-app/lighting-common REALPATH)
get_filename_component(GEN_DIR ${CHIP_ROOT}/zzz_generated/ REALPATH)

list(APPEND ZEPHYR_EXTRA_MODULES ${CHIP_ROOT}/config/esp32_zephyr/chip-module)
find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(chip-esp32-zephyr-lighting-example)

include(${CHIP_ROOT}/config/esp32_zephyr/app/enable-gnu-std.cmake OPTIONAL)
include(${CHIP_ROOT}/src/app/chip_data_model.cmake)

target_compile_options(app PRIVATE -Werror -Wno-error=maybe-uninitialized)

target_include_directories(app PRIVATE
                           main/include
                           ${LIGHTING_COMMON}
                           ${GEN_DIR}
                           ${GEN_DIR}/app-common
                           ${GEN_DIR}/lighting-app
                           ${CHIP_ROOT}/src
                           ${CHIP_ROOT}/src/include
                           ${CHIP_ROOT}/third_party/nlassert/repo/include)

target_sources(app PRIVATE
               main/AppTask.cpp
               main/main.cpp
               main/ZclCallbacks.cpp
               main/LEDWidget.cpp
               main/atomic.c
               ${CHIP_ROOT}/src/app/persistence/DeferredAttributePersistenceProvider.cpp)

chip_configure_data_model(app
    ZAP_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../lighting-common/lighting-app.zap
)
