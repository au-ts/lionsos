from .PDInfoSystem import PDInfoSystem, ClientInfo
from acacia import (
    System,
    Subsystem,
    ProtectionDomain,
    Channel,
    Map,
    MemoryRegion,
    ConfigStruct,
    SubsystemBuildError,
)
from dataclasses import dataclass
from typing import override, List, Dict

class IPCTracerSystem(PDInfoSystem):
    """
    An IPC Tracer system, which is an extension on the PDInfo system.
    This has a forced topology of the following:
        1. Receiver PD - This is in charge of receiving all inputs
        2. Sender PD - This is in charge of forwarding all inputs.
           This ensures that sends and recvs work as expected. You must set a
           badge calling convention to determine between ppcalls and sends/notifications.
           However, ppcalls cannot be chained. See appendix X for reasoning.
    """
    @override
    def __init__(self,
        sdf: System,
        receiver_pd: ProtectionDomain,
        sender_pd: ProtectionDomain,
        receiver_pd_all_config_section="pdc_pd_all_config",
        receiver_channel_all_config_section="pdc_channel_all_config",
        receiver_ipc_tracer_config_section="pdc_ipc_tracer_config",
        # The sender also gets the same information so that it knows who to send to if needed.
        sender_pd_all_config_section="pdc_pd_all_config",
        sender_channel_all_config_section="pdc_channel_all_config",
        sender_ipc_tracer_config_section="pdc_ipc_tracer_config",
    ):
        self.sender_pd = sender_pd
        self.sender_pd_all_config_section = sender_pd_all_config_section 
        self.sender_channel_all_config_section = sender_channel_all_config_section
        self.sender_ipc_tracer_config_section = sender_ipc_tracer_config_section
        self.receiver_pd = receiver_pd
        self.receiver_pd_all_config_section = receiver_pd_all_config_section 
        self.receiver_channel_all_config_section = receiver_channel_all_config_section
        self.receiver_ipc_tracer_config_section = receiver_ipc_tracer_config_section
        self.channel_id_to_source_child: Dict[int, ClientInfo] = dict()
        super().__init__(sdf, receiver_pd, receiver_pd_all_config_section, receiver_channel_all_config_section)

    @staticmethod
    def ipc_tracer_config_factory(target_file: str, target_section: str, channel_id_to_source_child: Dict[int, ClientInfo]) -> ConfigStruct:
        # A pseudo map from channel id to source child id.
        arraything: List[int] = [0 for _ in range(max(channel_id_to_source_child.keys())+1)]
        for key, value in channel_id_to_source_child.items():
            arraything[key] = value.id

        fields = dict(
            magic="ipcmagic" + chr(0x0),
            channel_id_to_source_child_id=arraything
        )
        return ConfigStruct(
            target_file=target_file,
            section_name=target_section,
            type_name="pdc_ipc_tracer_config_t",
            fields=fields
        )

    @override
    def add_client(self, client: "ProtectionDomain"):
        super().add_client(client)

    @override
    def get_clients(self) -> List[ProtectionDomain]:
        return super().get_clients()

    @override
    def connect_clients(self):
        # adds all clients as children.
        super().connect_clients()
        # now we need to modify all of our relevant channels
        original_channels = self._get_client_channels()

        # remove all of these channel's references
        # and add equivalent channels.
        for ch_ind, ch in enumerate(original_channels):
            self.sdf.channels.remove(ch.original)
            # intercept channels.
            # child_a -> receiver
            child_a_to_receiver_end = Channel.End(pd=self.receiver_pd, can_notify=False, can_pp=False, ch_id=ch_ind * 2)
            child_a_to_receiver_ch = Channel(self.sdf, end_a=ch.original.end_a ,end_b=child_a_to_receiver_end) 

            # child_a <- sender
            sender_to_child_a_end = Channel.End(pd=self.sender_pd, can_notify=True, can_pp=True, ch_id=ch_ind * 2)
            child_a_to_sender_end = Channel.End(
                pd=ch.original.end_a.pd,
                can_notify=False,
                can_pp=False,
                ch_id=ch.original.end_a.ch_id
            )
            sender_to_child_a_ch = Channel(self.sdf, sender_to_child_a_end, child_a_to_sender_end)

            # child_b -> receiver
            child_b_to_receiver_end = Channel.End(pd=self.receiver_pd, can_notify=False, can_pp=False, ch_id=ch_ind * 2 + 1)
            child_b_to_receiver_ch = Channel(self.sdf, end_a=child_b_to_receiver_end, end_b=ch.original.end_b)

            # child_b <- sender
            sender_to_child_b_end = Channel.End(pd=self.sender_pd, can_notify=True, can_pp=True, ch_id=ch_ind * 2 + 1)
            child_b_to_sender_end = Channel.End(
                pd=ch.original.end_b.pd,
                can_notify=False,
                can_pp=False,
                ch_id=ch.original.end_b.ch_id
            )

            sender_to_child_b_ch = Channel(self.sdf, sender_to_child_b_end, child_b_to_sender_end)

            self.channel_id_to_source_child[2 * ch_ind] = ch.end_a_client
            self.channel_id_to_source_child[2 * ch_ind + 1] = ch.end_b_client

    @override
    def generate_config_structs(self) -> List["ConfigStruct"]:
        """
        returns the config structs required for config.h in pdcontrol.
        """
        results = super().generate_config_structs()
        results += [
            self.pd_all_config_factory(self.sender_pd.prog_image, self.sender_pd_all_config_section, list(self._clients.values())),
            self.channel_all_config_factory(
                 self.sender_pd.prog_image,
                 self.sender_channel_all_config_section,
                 self._get_client_channels()),
            self.ipc_tracer_config_factory(self.sender_pd.prog_image, self.sender_ipc_tracer_config_section, self.channel_id_to_source_child),
            self.ipc_tracer_config_factory(self.receiver_pd.prog_image, self.receiver_ipc_tracer_config_section, self.channel_id_to_source_child),
        ]
        return results

    @override
    def build(self):
        super().build()
