# Python tooling for using pdcontrol with microkit_acacia.

from acacia import (
    System,
    Subsystem,
    ProtectionDomain,
    Channel,
    Map,
    MemoryRegion,
    DTBNode,
    DeviceTreeBlob,
    SchedulingProperties,
    ConfigStruct,
    SubsystemBuildError,
)
from typing import override, List

class PDControlSystem(Subsystem):
    @override
    def __init__(self, sdf: System, main_pd: ProtectionDomain, pd_all_config_section="pdc_pd_all_config"):
        self.main_pd = main_pd
        self.pd_all_config_section = pd_all_config_section
        super().__init__(sdf, main_pd.name, clients_allowed=True)

    def pd_config_factory(pd: ProtectionDomain, local_id: int) -> ConfigStruct:
        """
        local_id is the id from the perspective of the system's main_pd given.
        """
        assert pd.priority is not None
        fields = dict(name=pd.name, def_prio=pd.priority, id=local_id)
        return ConfigStruct(
                type_name="pdc_pd_config_t",
                fields=fields,
        )

    def pd_all_config_factory(target_file: str, section: str, pds: List[ProtectionDomain]) -> ConfigStruct:
        pd_config_structs: List["ConfigStruct"] = []
        for i, pd in enumerate(pds):
            pd_config_structs.append(
              PDControlSystem.pd_config_factory(pd, i) 
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

    @override
    def add_client(self, client: "ProtectionDomain"):
        super().add_client(client)

    @override
    def get_clients(self) -> List[ProtectionDomain]:
        return super().get_clients()

    @override
    def connect_clients(self):
        for pd in self.clients:
            self.main_pd.add_child_pd(pd)

    @override
    def generate_config_structs(self) -> List["ConfigStruct"]:
        """
        returns the config structs required for config.h in pdcontrol.
        """

        return (super().generate_config_structs() +
            [PDControlSystem.pd_all_config_factory(self.main_pd.prog_image, self.pd_all_config_section, self.clients)])

    @override
    def build(self):
        """
        Construct subsystem, initialising all PDs and connecting clients
        if possible.
        """
        if self.built:
            raise RuntimeError("Cannot build a subsystem more than once!")

        # Connect clients if needed
        if self.clients_allowed:
            self.connect_clients()
        self.built = True

