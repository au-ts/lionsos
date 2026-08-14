#!/home/louie/trustworthy_sys/lionsos/examples/webserver/.reload_venv/bin/python3

import os
import subprocess

import readline
import requests

import sys
import python_abi
import parse_realoading_pds


def partition_bytes(b: bytes, length: int):
    return [b[i:i + length] for i in range(0, len(b), length)]

def run_build(driver_name: str, elf_filename: str):
    cmd = ["make", "-f", "expanded.mk", f"driver={driver_name}", "all"] # so that we objcopy
    print(f"Starting build for: {driver_name} ({elf_filename})")
    try:
        subprocess.run(cmd, check=True)
        print(f"\nSuccessfully built {elf_filename}!")
    except subprocess.CalledProcessError:
        print(f"\nBuild failed for {elf_filename}.")
        sys.exit(1)

os.chdir(os.getenv("BUILD_DIR")) # the lionsOS build directory

(pd_to_elf_map, pd_to_index_map) = parse_realoading_pds.parse_protection_domains("webserver.system")

BASE_URL = "http://localhost:5555"
pds = list(pd_to_elf_map.keys())

pd_base = 1

def completer(text, state):
    options = [pd for pd in pds if pd.startswith(text)]
    if state < len(options):
        return options[state]
    return None

readline.set_completer(completer)
readline.parse_and_bind("tab: complete")

def main():
    while True:
        choice = input("Select a Protection Domain to inspect: ").strip()
        if choice in pds:
            break
        print(f"\nInvalid Selection. Allowed: {', '.join(pds)}\n")

    target_elf = pd_to_elf_map[choice]
    pd_id = pd_to_index_map[choice]

    run_build(choice, target_elf)

    if not os.path.exists(target_elf):
        print(f"Error: {target_elf} was not generated.")
        sys.exit(1)

    elf = python_abi.PyElfFile(target_elf)
    elf.update_segments(choice) # updates them with the correct data from microkit
    segments = elf.loadable_segments() # 
    (passive_vaddr, null) = elf.find_symbol("microkit_passive")

    print("the vaddr of passive is: ", passive_vaddr)

    print("we have: ", len(segments[0].data), " many bytes!")

    for segment in segments:
        resp = requests.post(
            f"{BASE_URL}/dynamic_loading",
            headers={
                "vaddr": str(segment.virt_addr),
                "seg_size": str(len(segment.data))
            },
            data=segment.data,
        )
        if resp.status_code >= 400:
            print("dynamic_loading failed:", resp.status_code, resp.text)
            sys.exit(1)

    print(pd_id)
    print(elf.get_entry_point())

    resp = requests.post(
        f"{BASE_URL}/update_pd",
        headers={
            "pd_id": str(pd_id),
            "entry": str(elf.get_entry_point()),
            "passive_vaddr": str(passive_vaddr)
        }
    )

    print("we have returned to reload.py!")
    print("Status:", resp.status_code)
    print("Response:", resp.text)


if __name__ == "__main__":
    main()