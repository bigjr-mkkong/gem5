import os

from gem5.components.boards.riscv_board import RiscvBoard
from gem5.components.cachehierarchies.classic.private_l1_private_l2_cache_hierarchy import (
    PrivateL1PrivateL2CacheHierarchy,
)
from gem5.components.memory.dramsim_3 import SingleChannel
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import (
    BootloaderResource,
    DiskImageResource,
    KernelResource,
)
from gem5.simulate.simulator import Simulator

FIRMWARE = "/sw-payload/opensbi/build/platform/generic/firmware/fw_jump.elf"
KERNEL = "/sw-payload/linux-riscv/vmlinux"  # RISC-V kernel ELF
DISK = "/sw-payload/rootfs/out-rootfs/rootfs.ext4"  # root filesystem image

assert os.path.exists(KERNEL), f"Kernel not found: {KERNEL}"
assert os.path.exists(DISK), f"Disk image not found: {DISK}"

processor = SimpleProcessor(
    cpu_type=CPUTypes.TIMING, num_cores=1, isa=ISA.RISCV
)

cache_hierarchy = PrivateL1PrivateL2CacheHierarchy(
    l1d_size="32KiB", l1i_size="32KiB", l2_size="512KiB"
)

# memory = SingleChannel("DDR4_4Gb_x4_2400_pim", size="4GiB")
memory = SingleChannel("DDR4_4Gb_x4_2400_pim", size="4GiB")

board = RiscvBoard(
    clk_freq="2GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

fw_res = BootloaderResource(local_path=FIRMWARE)
kernel_res = KernelResource(local_path=KERNEL)
disk_res = DiskImageResource(local_path=DISK)

board.set_kernel_disk_workload(
    bootloader=fw_res,
    kernel=kernel_res,
    disk_image=disk_res,
    kernel_args=[
        "console=ttyS0",
        "root=/dev/vda1",
        "rw",
        "init=/sbin/init",
    ],
)

sim = Simulator(board=board)

print("[cfg] launching simulation …")
sim.run()
print("[cfg] finished")
