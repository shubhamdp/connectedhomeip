/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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

#include <lib/shell/Commands.h>
#include <lib/shell/Engine.h>
#include <lib/shell/commands/Help.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <commands/common/Commands.h>
#include <commands/example/ExampleCredentialIssuerCommands.h>
#include <commands/pairing/Commands.h>
#include <commands/discover/Commands.h>

using namespace chip::DeviceLayer;

namespace chip {
namespace Shell {
namespace {

ExampleCredentialIssuerCommands credIssuerCommands;
Commands chipToolCommands;

CHIP_ERROR CtrlCommandHandler(int argc, char ** argv)
{
    int err = chipToolCommands.Run(argc + 1, argv - 1);
    return (err == 0) ? CHIP_NO_ERROR : CHIP_ERROR_INVALID_ARGUMENT;
}

} // namespace

void RegisterChipControllerCommands()
{
    registerCommandsDiscover(chipToolCommands);
    registerCommandsPairing(chipToolCommands, &credIssuerCommands);
    chipToolCommands.Init();

    static const shell_command_t ctrlCommand = { &CtrlCommandHandler, "chip-tool", "CHIP Controller Commands, exactly similar like how to use chip-tool" };

    Engine::Root().RegisterCommands(&ctrlCommand, 1);
}

} // namespace Shell
} // namespace chip
