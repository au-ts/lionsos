#!/home/louie/trustworthy_sys/lionsos/examples/webserver/.reload_venv/bin/python3

import xml.etree.ElementTree as ET

# add my source later so I can edit, for xml file sorta stuff
def parse_protection_domains(xml_file):
    tree = ET.parse(xml_file)
    root = tree.getroot()

    pd_to_elf = {}
    pd_to_index = {}

    def visit_pd(pd_elem):
        name = pd_elem.get("name")
        pd_id = pd_elem.get("id")

        prog = pd_elem.find("program_image")
        if prog is not None and name:
            pd_to_elf[name] = prog.get("path")

        if name and pd_id:
            pd_to_index[name] = int(pd_id)

        for child in pd_elem.findall("protection_domain"):
            visit_pd(child)

    reloader_pd = root.find(".//protection_domain[@name='reloader']")
    if reloader_pd is None:
        raise ValueError("No 'reloader' protection_domain found")

    visit_pd(reloader_pd)

    return pd_to_elf, pd_to_index