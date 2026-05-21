import os

import m5
from m5.objects import (
    CommMonitor,
    MemTraceProbe,
)

from gem5.components.boards.my_rv64_board import MyRiscvBoard
from gem5.components.cachehierarchies.classic.no_cache import (
    NoCache,
)
from gem5.components.cachehierarchies.classic.private_l1_cache_hierarchy import (
    PrivateL1CacheHierarchy,
)
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


class TracedSingleChannel(SingleChannel):
    def __init__(self, mem_type: str, size: str):
        super().__init__(mem_type, size)

        # 1. Create the monitor and trace probe as a child of the memory
        self.comm_monitor = CommMonitor()
        self.comm_monitor.trace = MemTraceProbe(
            trace_file="dram_memory_trace.trc", trace_compress=False
        )

        # Flag to prevent connecting the ports multiple times
        self._ports_connected = False

    def get_mem_ports(self):
        # 2. Get the real DRAM ports from the parent SingleChannel class
        original_ports = super().get_mem_ports()
        addr_range = original_ports[0][0]
        dram_port = original_ports[0][1]

        # 3. Wire the monitor's memory side to the actual DRAM ONLY ONCE
        if not self._ports_connected:
            self.comm_monitor.mem_side_port = dram_port
            self._ports_connected = True

        # 4. Hand the monitor's CPU port back to the cache hierarchy
        return [(addr_range, self.comm_monitor.cpu_side_port)]


# FIRMWARE = "/sw-payload/gem5-sw/opensbi/build/platform/generic/firmware/fw_jump.elf"
FIRMWARE = (
    "/sw-payload/gem5-sw/zephyr-proj/mini-cpubench/build/zephyr/zephyr.elf"
)
KERNEL = (
    "/sw-payload/gem5-sw/zephyr-proj/mini-cpubench/build/zephyr/zephyr.elf"
)
DISK = "/sw-payload/gem5-sw/rootfs/out-rootfs/pseudo_disk.img"  # root filesystem image

assert os.path.exists(KERNEL), f"Kernel not found: {KERNEL}"
assert os.path.exists(DISK), f"Disk image not found: {DISK}"

processor = SimpleProcessor(
    cpu_type=CPUTypes.TIMING, num_cores=1, isa=ISA.RISCV
)

# cache_hierarchy = PrivateL1PrivateL2CacheHierarchy(
#     l1d_size="32KiB", l1i_size="32KiB", l2_size="512KiB"
# )

# cache_hierarchy = NoCache()
cache_hierarchy = PrivateL1CacheHierarchy(
    l1d_size="8KiB", l1i_size="32KiB"
)

# Use our traced memory wrapper
memory = TracedSingleChannel("DDR4_4Gb_x4_2400", size="4GiB")

board = MyRiscvBoard(
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
)

sim = Simulator(board=board)

print("[cfg] launching simulation …")
# sim.run()

sim._instantiate()

while True:
    event = m5.simulate()
    exit_msg = event.getCause()

    if exit_msg == "workbegin":
        print("Profile begin catched")
        board.memory.comm_monitor.set_stats(1)
    elif exit_msg == "workend":
        print("Profile end catched")
        board.memory.comm_monitor.set_stats(0)

    elif (
        exit_msg == "exiting with last active thread context"
        or exit_msg == "workload finished"
    ):
        break
    else:
        print(f"[cfg] Simulation stopped due to: {exit_msg}")
        break
print("[cfg] finished")
