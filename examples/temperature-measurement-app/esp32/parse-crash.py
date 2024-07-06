#!/usr/bin/env python3
#
# Copyright (c) 2024 Project CHIP Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
"""
Parses the core dump summary for ESP32 SoCs
"""

from construct import Struct, Bytes, Int32ul, Array, Int8ul, Flag
import click

# Define Construct format for esp_core_dump_bt_info_t (RISC-V)
riscv_bt_info_format = Struct(
    "stackdump" / Bytes(1024),
    "dump_size" / Int32ul,
)

# Define Construct format for esp_core_dump_summary_extra_info_t (RISC-V)
riscv_extra_info_format = Struct(
    "mstatus" / Int32ul,
    "mtvec" / Int32ul,
    "mcause" / Int32ul,
    "mtval" / Int32ul,
    "ra" / Int32ul,
    "sp" / Int32ul,
    "exc_a" / Array(8, Int32ul),
)

# Define Construct format for esp_core_dump_bt_info_t (Xtensa)
xtensa_bt_info_format = Struct(
    "bt" / Array(16, Int32ul),
    "depth" / Int32ul,
    "corrupted" / Flag
)

# Define Construct format for esp_core_dump_summary_extra_info_t (Xtensa)
xtensa_extra_info_format = Struct(
    "exc_cause" / Int32ul,
    "exc_vaddr" / Int32ul,
    "exc_a" / Array(16, Int32ul),
    "epcx" / Array(6, Int32ul),
    "epcx_reg_bits" / Int8ul
)

def riscv_pretty_print(parsed_data):
    print("Exception TCB:", hex(parsed_data.exc_tcb))
    print("Exception Task:", parsed_data.exc_task.decode('utf-8'))
    print("Exception PC:", hex(parsed_data.exc_pc))
    print("Backtrace Info:")
    print("  Dump Size:", parsed_data.exc_bt_info.dump_size)
    print("  Stack Dump:", parsed_data.exc_bt_info.stackdump[:parsed_data.exc_bt_info.dump_size].hex())
    print("Core Dump Version:", parsed_data.core_dump_version)
    print("App ELF SHA256:", parsed_data.app_elf_sha256.decode('utf-8'))
    print("Extra Info:")
    print("  mstatus:", hex(parsed_data.ex_info.mstatus))
    print("  mtvec:", hex(parsed_data.ex_info.mtvec))
    print("  mcause:", hex(parsed_data.ex_info.mcause))
    print("  mtval:", hex(parsed_data.ex_info.mtval))
    print("  ra:", hex(parsed_data.ex_info.ra))
    print("  sp:", hex(parsed_data.ex_info.sp))
    print("  exc_a:", [hex(reg) for reg in parsed_data.ex_info.exc_a])


def xtensa_pretty_print(parsed_data, elf_fd):
    print("Exception TCB:", hex(parsed_data.exc_tcb))
    print("Exception Task:", parsed_data.exc_task.decode('utf-8'))
    print("Exception PC:", hex(parsed_data.exc_pc))
    print("Backtrace Info:")
    print("  Corrupted:", parsed_data.exc_bt_info.corrupted)
    backtrace = [hex(bt) for bt in parsed_data.exc_bt_info.bt]
    print("  Backtrace:", backtrace[:parsed_data.exc_bt_info.depth])
    print("Core Dump Version:", parsed_data.core_dump_version)
    print("App ELF SHA256:", parsed_data.app_elf_sha256.decode('utf-8'))
    print("Extra Info:")
    print("  exc_cause:", hex(parsed_data.ex_info.exc_cause))
    print("  exc_vaddr:", hex(parsed_data.ex_info.exc_vaddr))
    print("  exc_a:", [hex(reg) for reg in parsed_data.ex_info.exc_a])
    print("  epcx:", [hex(reg) for reg in parsed_data.ex_info.epcx])
    print("  epcx_reg_bits:", bin(parsed_data.ex_info.epcx_reg_bits))

def pretty_print(parsed_data, arch, elf_fd):
    if arch == "riscv":
        riscv_pretty_print(parsed_data)
    elif arch == "xtensa":
        xtensa_pretty_print(parsed_data, elf_fd)

@click.command()
@click.option('--arch', type=click.Choice(['riscv', 'xtensa']), required=True, help='Select the architecture')
@click.option('--core-summary', type=click.File('rb'), required=True, help='Core dump summary file')
@click.option('--elf-file', type=click.File('rb'), required=True, help='ELF file to resolve PCs')
def parse_core_dump_summary(arch, core_summary, elf_file):
    # Select Construct formats based on architecture
    if arch == "riscv":
        bt_info_format = riscv_bt_info_format
        extra_info_format = riscv_extra_info_format
    elif arch == "xtensa":
        bt_info_format = xtensa_bt_info_format
        extra_info_format = xtensa_extra_info_format

    summary_format = Struct(
        "exc_tcb" / Int32ul,
        "exc_task" / Bytes(16),
        "exc_pc" / Int32ul,
        "exc_bt_info" / bt_info_format,
        "core_dump_version" / Int32ul,
        "app_elf_sha256" / Bytes(17),
        "ex_info" / extra_info_format,
    )

    # Read binary data from file or use binary data directly
    binary_data = core_summary.read()

    # Parse binary data using Construct format
    parsed_data = summary_format.parse(binary_data)
    pretty_print(parsed_data, arch, elf_file)


if __name__ == '__main__':
    parse_core_dump_summary()
