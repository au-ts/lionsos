# Python tooling for using pdcontrol with microkit_acacia.
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

@dataclass
class ClientInfo:
    pd: ProtectionDomain
    id: int

    def to_config_struct(self) -> ConfigStruct:
        """
        id is the id from the perspective of the system's main_pd given.
        """
        assert self.pd.priority is not None
        fields = dict(name=self.pd.name, def_prio=self.pd.priority, id=self.id)
        return ConfigStruct(
                type_name="pdc_pd_config_t",
                fields=fields,
        )


@dataclass
class ChannelInfo:
    original: Channel
    end_a_client: ClientInfo
    end_b_client: ClientInfo

    def to_config_struct(self) -> ConfigStruct:
        fields = dict(
            end_a_channel_id=self.original.end_a.ch_id,
            end_b_channel_id=self.original.end_b.ch_id,
            end_a_child_id=self.end_a_client.id,
            end_b_child_id=self.end_b_client.id,
        )
        return ConfigStruct(
            type_name="pdc_channel_config_t",
            fields=fields
        )

class PDInfoSystem(Subsystem):
    @override
    def __init__(self,
        sdf: System,
        main_pd: ProtectionDomain,
        pd_all_config_section="pdc_pd_all_config",
        channel_all_config_section="pdc_channel_all_config",
    ):
        self.main_pd = main_pd
        self.pd_all_config_section = pd_all_config_section
        self.channel_all_config_section = channel_all_config_section
        # int is the client's name.
        self._clients: Dict[str, ClientInfo] = {}
        self._connected = False
        super().__init__(sdf, main_pd.name, clients_allowed=True)


    @staticmethod
    def channel_all_config_factory(target_file: str, section: str, channels: List[ChannelInfo]) -> ConfigStruct:

        configs: List[ConfigStruct] = []
        for ch in channels:
            configs.append(ch.to_config_struct())

        fields = dict(
            magic="chanmagic" + chr(0x0),
            num_channels=len(configs),
            configs=configs,
        )

        return ConfigStruct(
            type_name="pdc_channel_all_config_t",
            section_name=section,
            fields=fields,
            target_file=target_file,
        )

    @staticmethod
    def pd_all_config_factory(target_file: str, section: str, clients: List[ClientInfo]) -> ConfigStruct:

        pd_config_structs: List["ConfigStruct"] = []
        for client in clients:
            pd_config_structs.append(
                client.to_config_struct()
            )

        fields = dict(
            magic="pdmagic" + chr(0x0),
            num_clients=len(pd_config_structs),
            configs=pd_config_structs,
        )

        return ConfigStruct(
            type_name="pdc_pd_all_config_t",
            section_name=section,
            fields=fields,
            target_file=target_file,
        )

    def _get_client_channels(self) -> List[ChannelInfo]:
        # not technically supported by acacia api, and will be prone to breaking.
        channels_sorted = sorted(self.sdf.channels,
            key=lambda ch: ch.end_a.pd.name + ch.end_b.pd.name)

        our_channels: List[ChannelInfo] = []

        # find the channels that are contained in this system.
        for chan in channels_sorted:
            if chan.end_a.pd.name in self._clients and chan.end_b.pd.name in self._clients:
                our_channels.append(ChannelInfo(
                    chan,
                    self._clients[chan.end_a.pd.name],
                    self._clients[chan.end_b.pd.name],
                ))
        return our_channels

    @override
    def add_client(self, client: "ProtectionDomain"):
        super().add_client(client)
        assert client.name not in self._clients
        self._clients[client.name] = ClientInfo(client, len(self._clients))

    @override
    def get_clients(self) -> List[ProtectionDomain]:
        return super().get_clients()

    @override
    def connect_clients(self):
        assert not self._connected
        for pd in self.clients:
            self.main_pd.add_child_pd(pd)
        self._connected = True

    @override
    def generate_config_structs(self) -> List["ConfigStruct"]:
        """
        returns the config structs required for config.h in pdcontrol.
        """
        return (super().generate_config_structs() +
            [self
                .pd_all_config_factory(self.main_pd.prog_image, self.pd_all_config_section, list(self._clients.values())),
             self
                 .channel_all_config_factory(
                     self.main_pd.prog_image,
                     self.channel_all_config_section,
                     self._get_client_channels()),
            ])

    @override
    def build(self):
        super().build()

