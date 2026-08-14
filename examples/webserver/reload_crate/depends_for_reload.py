#!/home/louie/trustworthy_sys/lionsos/examples/webserver/.reload_venv/bin/python3
import struct
import parse_realoading_pds
import os
from collections import defaultdict

# also there is 8 but we are 1 indexing everything
MAX_PID = 9 # needs to be consistent with what is in the reloader

(pd_to_elf_map, pd_to_index_map) = parse_realoading_pds.parse_protection_domains("webserver.system")

class DependencyMatrix:
    def __init__(self):
        self.matrix = [[0 for _ in range(MAX_PID)] for _ in range(MAX_PID)] # this is 0 indexed but I am treating it differently as if it is 1-indexed
        self.counters = [0 for _ in range(MAX_PID)]

        self.graph = defaultdict(list)

    def append_dependency(self, src, dst):
        src_pid = pd_to_index_map[src]
        dst_pid = pd_to_index_map[dst]
        self.graph[src_pid].append(dst_pid) # for the bfs

    def addEdge(self, u, v):
        self.graph[u].append(v)

    # make sure the visited stuff is in the correct order for reloading
    def BFS(self, s):
        visited = [False] * MAX_PID
        queue = []
        queue.append(s)
        visited[s] = True

        src_id = s
        while queue:
            s = queue.pop(0)
            
            print("we are assigning the pid ", s, " to ", src_id)
            index = self.counters[src_id]
            self.counters[src_id] += 1
            self.matrix[src_id][index] = s

            for i in self.graph[s]:
                if not visited[i]:
                    queue.append(i)
                    visited[i] = True

    def get_matrix(self):
        return self.matrix


def assign_dependencies():
    dm = DependencyMatrix()

    dm.append_dependency("ethernet_driver", "net_virt_tx")
    dm.append_dependency("ethernet_driver", "net_virt_rx")

    # dm.append_dependency("net_virt_tx", "nfs_net_copier")
    dm.append_dependency("net_virt_rx", "nfs_net_copier")

    # dm.append_dependency("net_virt_tx", "micropython_net_copier") this should actually have micropython right, I have this stuff wrong
    dm.append_dependency("net_virt_rx", "micropython_net_copier")

    # dm.append_dependency("micropython_net_copier", "micropython") // unfortunately I cannot add this as a child yet
    # dm.append_dependency("nfs_net_copier" ,"nfs")

    dm.append_dependency("serial_driver", "serial_virt_tx")

    return dm

def emit_binary(matrix, filename="reloading_dependencies.data"):
    bytes = bytearray()
    with open(filename, "wb") as f:
        for i in range(MAX_PID):
            for j in range(MAX_PID):
                bytes.append(matrix[i][j])
        print(bytes)
        f.write(bytes)


if __name__ == '__main__':
    dm = assign_dependencies()
    matrix = dm.get_matrix()
    for i in range(MAX_PID):
        dm.BFS(i)
    emit_binary(matrix)