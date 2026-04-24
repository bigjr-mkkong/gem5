# Copyright (c) 2021 The Regents of the University of California
# Copyright (c) 2022 EXAscale Performance SYStems (EXAPSYS)
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
from typing import (
    List,
    Optional,
)

import m5
from m5.objects import (
    AddrRange,
    BadAddr,
    Bridge,
    CowDiskImage,
    Frequency,
    GenericRiscvPciHost,
    HiFive,
    IGbE_e1000,
    IOXBar,
    PMAChecker,
    Port,
    RawDiskImage,
    RiscvBootloaderKernelWorkload,
    RiscvMmioVirtIO,
    RiscvRTC,
    VirtIOBlock,
    VirtIORng,
)
from m5.util.fdthelper import (
    Fdt,
    FdtNode,
    FdtProperty,
    FdtPropertyStrings,
    FdtPropertyWords,
    FdtState,
)

from ...components.boards.se_binary_workload import SEBinaryWorkload
from ...isas import ISA
from ...resources.resource import AbstractResource
from ...utils.override import overrides
from ..cachehierarchies.abstract_cache_hierarchy import AbstractCacheHierarchy
from ..memory.abstract_memory_system import AbstractMemorySystem
from ..processors.abstract_processor import AbstractProcessor
from .abstract_system_board import AbstractSystemBoard
from .kernel_disk_workload import KernelDiskWorkload


class MyRiscvBoard(AbstractSystemBoard, KernelDiskWorkload, SEBinaryWorkload):
    """
    A board capable of full system simulation for RISC-V.

    At a high-level, this is based on the HiFive Unmatched board from SiFive.

    This board assumes that you will be booting Linux.

    **Limitations**
    * Only works with classic caches
    """

    def __init__(
        self,
        clk_freq: str,
        processor: AbstractProcessor,
        memory: AbstractMemorySystem,
        cache_hierarchy: AbstractCacheHierarchy,
    ) -> None:
        super().__init__(clk_freq, processor, memory, cache_hierarchy)

        if processor.get_isa() != ISA.RISCV:
            raise Exception(
                "The RISCVBoard requires a processor using the"
                "RISCV ISA. Current processor ISA: "
                f"'{processor.get_isa().name}'."
            )

    @overrides(AbstractSystemBoard)
    def _setup_board(self) -> None:
        if self.is_fullsystem():
            self.workload = RiscvBootloaderKernelWorkload()

            # Contains a CLINT, PLIC, UART, and some functions for the dtb, etc.
            self.platform = HiFive()
            # Note: This only works with single threaded cores.
            self.platform.plic.hart_config = ",".join(
                ["MS" for _ in range(self.processor.get_num_cores())]
            )
            self.platform.attachPlic()
            self.platform.clint.num_threads = self.processor.get_num_cores()

            # Add the RTC
            # TODO: Why 100MHz? Does something else need to change when this does?
            self.platform.rtc = RiscvRTC(
                frequency=Frequency("100MHz")
            )  # page 77, section 7.1
            self.platform.clint.int_pin = self.platform.rtc.int_pin

            # Incoherent I/O bus
            self.iobus = IOXBar()
            self.iobus.badaddr_responder = BadAddr()
            self.iobus.default = self.iobus.badaddr_responder.pio

            # The virtio disk
            self.disk = RiscvMmioVirtIO(
                vio=VirtIOBlock(),
                interrupt_id=0x8,
                pio_size=4096,
                pio_addr=0x10008000,
            )

            # The virtio rng
            self.rng = RiscvMmioVirtIO(
                vio=VirtIORng(),
                interrupt_id=0x8,
                pio_size=4096,
                pio_addr=0x10007000,
            )

            # Note: This overrides the platform's code because the platform
            # isn't general enough.
            self._on_chip_devices = [self.platform.clint, self.platform.plic]
            self._off_chip_devices = [self.platform.uart, self.disk, self.rng]

        else:
            # SE mode board setup
            pass

    def _setup_io_devices(self) -> None:
        """Connect the I/O devices to the I/O bus."""
        # Add PCI
        self.platform.pci_host.pio = self.iobus.mem_side_ports

        # Add Ethernet card
        self.ethernet = IGbE_e1000(
            pci_bus=0, pci_dev=0, pci_func=0, InterruptLine=1, InterruptPin=1
        )

        self.ethernet.host = self.platform.pci_host
        self.ethernet.pio = self.iobus.mem_side_ports
        self.ethernet.dma = self.iobus.cpu_side_ports

        if self.get_cache_hierarchy().is_ruby():
            for device in self._off_chip_devices + self._on_chip_devices:
                device.pio = self.iobus.mem_side_ports

        else:
            for device in self._off_chip_devices:
                device.pio = self.iobus.mem_side_ports
            for device in self._on_chip_devices:
                device.pio = self.get_cache_hierarchy().get_mem_side_port()

            self.bridge = Bridge(delay="10ns")
            self.bridge.mem_side_port = self.iobus.cpu_side_ports
            self.bridge.cpu_side_port = (
                self.get_cache_hierarchy().get_mem_side_port()
            )
            self.bridge.ranges = [
                AddrRange(dev.pio_addr, size=dev.pio_size)
                for dev in self._off_chip_devices
            ]

            # PCI
            self.bridge.ranges.append(AddrRange(0x2F000000, size="16MiB"))
            self.bridge.ranges.append(AddrRange(0x30000000, size="256MiB"))
            self.bridge.ranges.append(AddrRange(0x40000000, size="512MiB"))

    def _setup_pma(self) -> None:
        """Set the PMA devices on each core."""

        uncacheable_range = [
            AddrRange(dev.pio_addr, size=dev.pio_size)
            for dev in self._on_chip_devices + self._off_chip_devices
        ]

        # PCI
        uncacheable_range.append(AddrRange(0x2F000000, size="16MiB"))
        uncacheable_range.append(AddrRange(0x30000000, size="256MiB"))
        uncacheable_range.append(AddrRange(0x40000000, size="512MiB"))

        # TODO: Not sure if this should be done per-core like in the example
        for cpu in self.get_processor().get_cores():
            cpu.get_mmu().pma_checker = PMAChecker(
                uncacheable=uncacheable_range
            )

    @overrides(AbstractSystemBoard)
    def has_dma_ports(self) -> bool:
        return False

    @overrides(AbstractSystemBoard)
    def get_dma_ports(self) -> List[Port]:
        raise Exception(
            "Cannot execute `get_dma_ports()`: Board does not have DMA ports "
            "to return. Use `has_dma_ports()` to check this."
        )

    @overrides(AbstractSystemBoard)
    def has_io_bus(self) -> bool:
        return self.is_fullsystem()

    @overrides(AbstractSystemBoard)
    def get_io_bus(self) -> IOXBar:
        if self.has_io_bus():
            return self.iobus
        else:
            raise Exception(
                "Cannot execute `get_io_bus()`: Board does not have an I/O "
                "bus to return. Use `has_io_bus()` to check this."
            )

    @overrides(AbstractSystemBoard)
    def has_coherent_io(self) -> bool:
        return self.is_fullsystem()

    @overrides(AbstractSystemBoard)
    def get_mem_side_coherent_io_port(self) -> Port:
        if self.has_coherent_io():
            return self.iobus.mem_side_ports
        else:
            raise Exception(
                "Cannot execute `get_mem_side_coherent_io_port()`: Board does "
                "not have any I/O ports to return. Use `has_coherent_io()` to "
                "check this."
            )

    @overrides(AbstractSystemBoard)
    def _setup_memory_ranges(self):
        memory = self.get_memory()
        mem_size = memory.get_size()
        self.mem_ranges = [AddrRange(start=0x80000000, size=mem_size)]
        memory.set_memory_range(self.mem_ranges)

    @overrides(KernelDiskWorkload)
    def get_disk_device(self):
        return "/dev/vda"

    @overrides(AbstractSystemBoard)
    def _pre_instantiate(self, full_system: Optional[bool] = None):
        # This is a bit of a hack necessary to get the RiscDemoBoard working
        # At the time of writing the RiscvBoard does not support SE mode so
        # this branch looks pointless. However, the RiscvDemoBoard does and
        # needs this logic in place.
        #
        # This should be refactored in the future as part of a chance to have
        # all boards support both FS and SE modes.
        if self.is_fullsystem():
            if len(self._bootloader) > 0:
                self.workload.bootloader_addr = 0x0
                self.workload.bootloader_filename = self._bootloader[0]
                self.workload.kernel_addr = 0x80200000
                self.workload.entry_point = (
                    0x80000000  # Bootloader starting point
                )
            else:
                self.workload.kernel_addr = 0x0
                self.workload.entry_point = 0x80000000

            # Set up the device tree. We need to wait until pre-instantiate to
            # do this since the workload could change until this point.
            # Default DTB address if bbl is built with --with-dts option
            self.workload.dtb_addr = 0x87E00000

            self.workload.dtb_filename = os.path.join(
                m5.options.outdir, "device.dtb"
            )

        super()._pre_instantiate(full_system=full_system)

    @overrides(KernelDiskWorkload)
    def _add_disk_to_board(self, disk_image: AbstractResource):
        image = CowDiskImage(
            child=RawDiskImage(read_only=True), read_only=False
        )
        image.child.image_file = disk_image.get_local_path()
        self.disk.vio.image = image

        # Note: The below is a bit of a hack. We need to wait to generate the
        # device tree until after the disk is set up. Now that the disk and
        # workload are set, we can generate the device tree file.
        self._setup_io_devices()
        self._setup_pma()

    @overrides(KernelDiskWorkload)
    def get_default_kernel_args(self) -> List[str]:
        return [
            "console=ttyS0",
            "root={root_value}",
            "disk_device={disk_device}",
            "rw",
        ]
