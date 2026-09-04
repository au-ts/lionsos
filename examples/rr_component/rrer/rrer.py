from typing import List, Dict
from acacia.arch import aarch64
from acacia import ProtectionDomain, MemoryRegion, Map, System, Channel, Subsystem, PageTables, CSpace, Cap
import xml.etree.ElementTree as et
from dataclasses import dataclass, field
from abc import ABC
from copy import deepcopy
import pathlib

WORD_SIZE = 8

@dataclass
class RRChild:
    id: int
    priority: int
    sched_state: int = 0 # Always 0 to start.

    def to_bytes(self, endian="little"):
        assert self.sched_state == 0
        return (
            self.id.to_bytes(WORD_SIZE, endian)
            + self.priority.to_bytes(WORD_SIZE, endian)
            + self.sched_state.to_bytes(WORD_SIZE, endian)
        )

@dataclass
class RRData:
    """
    Word format is the following:
        [0, 1): Number of children N
        [1 .. N*3 + 1): Array of children
        [+1] Number of channels
        [..] channel to child_id
        [N*3 + 1 .. N*4 + 1): Stuff for storing pointers for the scheduler
        TODO: Why can't i just objcopy this in?
        We could also embed all children names if we wanted to.
    """
    children: List[RRChild]
    channel_id_to_target_child_id: List[int]

    def serialise(self, childrenpath: pathlib.Path) -> int:
        """returns size of file in bytes, and writes the data as a binary file."""
        # Serialise the num children.
        b = len(self.children).to_bytes(WORD_SIZE, "little")

        # Serialise the array of children.
        for ch in self.children:
            b += ch.to_bytes()

        # Serialise the number of channels.
        b += len(self.channel_id_to_target_child_id).to_bytes(WORD_SIZE, "little")

        # serialise the channels to child id.
        for id in self.channel_id_to_target_child_id:
            b += id.to_bytes(WORD_SIZE, "little")

        # Give some memory for the scheduler queue
        for _ in range(len(self.children)):
            b += (0).to_bytes(WORD_SIZE, "little")

        with open(childrenpath, "wb") as chd:
            chd.write(b)
        return len(b)

class RRSystem(System):
    """
    A hooked version of system, which should be techinically used like a subsystem.
    Intercepts all channels, and forces them to pass through a central PD.
    """
    def __init__(self, sdf: System):
        """
        Takes in a system to copy the architecture and paddr_top and any other relevant information.
        """
        super().__init__(sdf.arch, paddr_top=sdf.paddr_top, dtb=sdf.dtb)

    def _add_pd(self, pd: "ProtectionDomain"):
        self.pds.add(pd)

    def _add_channel(self, channel: "Channel"):
        # intercept the channel.
        self.channels.add(channel)

    def _add_memory_region(self, mr: "MemoryRegion"):
        self.mrs.add(mr)

    def _add_subsystem(self, subsystem: Subsystem):
        self.subsystems.append(subsystem)

    def resolve_subsystems(self):
        super().resolve_subsystems()

    def make_config_structs(self, build_dir: pathlib.Path = pathlib.Path("./")):
        raise RuntimeError("Unsupported action")

    def render(self) -> et.Element:
        raise RuntimeError("Unsupported action")

    def write_xml_file(self, path: pathlib.Path):
        raise RuntimeError("Unsupported action")

    def transfer(self, sdf: System, output_path: pathlib.Path | str):
        """
        Transfers the rrsystem into the sdf system, making all endpoints get intercepted by rrer.
        and collects pagetables.
        Sets up the rr_* protection domains.
        """
        if isinstance(output_path, str):
            output_path = pathlib.Path(output_path)

        main = ProtectionDomain(sdf, "rr_main", "rr_main.elf", priority=253)
        main.add_vpmu(0)
        sender = ProtectionDomain(sdf, "rr_sender", "rr_sender.elf", priority=251)
        block_checker = ProtectionDomain(sdf, "rr_block_checker", "rr_block_checker.elf", priority=250)
        assert self.arch == sdf.arch
        assert main.sdf == sdf
        assert not (main in self.pds)
        assert main in sdf.pds

        if not self.system_assembled:
            self.resolve_subsystems()

        # set up the rr_* protection domains
        # block checker just needs to be able to notify main
        block_checker_main_ch = Channel(sdf,
            Channel.End(pd=main, can_notify=False, can_pp=False, ch_id=60, setvar_id="blocker_ch"),
            Channel.End(pd=block_checker, can_notify=True, can_pp=False, ch_id=61, setvar_id="main_ch")
        )

        # The sender thread must be a child of main.
        main.add_child_pd(sender, child_id=61)
        # We also should restart the blocker.
        main.add_child_pd(block_checker, child_id=60)

        # We'll just allow everything for now
        sender_main_ch = Channel(sdf,
            Channel.End(pd=main, can_notify=True, can_pp=True, ch_id=59, setvar_id="sender_ch"),
            Channel.End(pd=sender, can_notify=True, can_pp=True, ch_id=61, setvar_id="main_ch")
        )

        # Setup the per-thread recv queue memory regions.
        # This is done as memory region on purpose because this is shared
        # between PDs.
        per_thread_recv_queue = MemoryRegion(
            sdf,
            "per_thread_recv_queue",
            # allocate 0x1000 per thread/queue structure
            size=0x1000 * len(self.pds)
        )

        sender_recv_queue_map = Map(
            per_thread_recv_queue,
            0x500000,
            permissions="rw",
            setvar_vaddr="per_thread_recv_queue_mem",
            setvar_size="per_thread_recv_queue_size",
        )
        sender.add_map(sender_recv_queue_map)

        main_recv_queue_map = Map(
            per_thread_recv_queue,
            0x500000,
            permissions="rw",
            setvar_vaddr="per_thread_recv_queue_mem",
            setvar_size="per_thread_recv_queue_size",
        )

        main.add_map(main_recv_queue_map)

        pts = PageTables(setvar="table_metadata")
        csp = CSpace()
        # cspace slot 0 is reserved
        # cspace slot 1 is always self_tcb
        csp.add_cap(Cap.TCB, 1, main.name)
        children: List[RRChild] = []
        child_name_to_child_id : Dict[str, int] = dict()
        for (child_id, pd) in enumerate(self.pds):
            pd.sdf = sdf
            sdf._add_pd(pd)
            pts.add_entry(pd.name, child_id)

            assert pd in sdf.pds
            assert pd in main.sdf.pds

            main.add_child_pd(pd, child_id);
            children.append(RRChild(child_id, pd.priority))
            child_name_to_child_id[pd.name] = child_id

        main.add_pagetables(pts)
        main.add_cspace(csp)

        # Now we should keep track of endpoints so we know who to forward to.
        # index is channel_id, value is child_id
        channel_id_to_target_child_id: List[int] = []
        print(f"Num channels: {len(self.channels)}")
        ch_ind = 0
        for channel in self.channels:
            # TODO: Add support for checking if channels are uni-directional.
            # child_a has target child_b, at ch_ind
            channel_id_to_target_child_id.append(child_name_to_child_id[channel.end_b.pd.name])
            # child_b has target child_a, at ch_ind + 1
            channel_id_to_target_child_id.append(child_name_to_child_id[channel.end_a.pd.name])

            # intercept channels.
            # child_a -> main
            child_a_to_main_end = Channel.End(pd=main, can_notify=False, can_pp=False, ch_id=ch_ind)
            child_a_to_main_ch = deepcopy(channel)
            # end_b!
            child_a_to_main_ch.end_b = child_a_to_main_end
            child_a_to_main_ch.sdf = sdf
            sdf._add_channel(child_a_to_main_ch)

            #  child_a <- sender
            sender_to_child_a_end = Channel.End(pd=sender, can_notify=True, can_pp=True, ch_id=ch_ind)
            child_a_to_sender_end = Channel.End(
                pd=channel.end_a.pd,
                can_notify=False,
                can_pp=False,
                ch_id=channel.end_a.ch_id
            )
            sender_to_child_a_ch = Channel(sdf, sender_to_child_a_end, child_a_to_sender_end)



            # child_b -> main
            child_b_to_main_end = Channel.End(pd=main, can_notify=False, can_pp=False, ch_id=ch_ind+1)
            child_b_to_main_ch = deepcopy(channel)
            # end_a!
            child_b_to_main_ch.end_a = child_b_to_main_end
            child_b_to_main_ch.sdf = sdf
            sdf._add_channel(child_b_to_main_ch)

            # child_b <- sender
            sender_to_child_b_end = Channel.End(pd=sender, can_notify=True, can_pp=True, ch_id=ch_ind+1)
            child_b_to_sender_end = Channel.End(
                pd=channel.end_b.pd,
                can_notify=False,
                can_pp=False,
                ch_id=channel.end_b.ch_id
            )
            sender_to_child_b_ch = Channel(sdf, sender_to_child_b_end, child_b_to_sender_end)

            ch_ind += 2

        for mr in self.mrs:
            sdf._add_memory_region(mr)

        children_data = RRData(children, channel_id_to_target_child_id)
        DATAPATH = output_path / "rr_children.data"
        children_data.serialise(DATAPATH)

        # This data can and should be objcopied?
        # For now just use mr prefill.
        rr_mr = MemoryRegion(sdf, "rr_children_data", prefill_path=DATAPATH)
        main.add_map(Map(rr_mr, 0x400000, "rw", setvar_vaddr="children_data_mem"))
