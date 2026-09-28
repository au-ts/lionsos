Hello from a file on the FAT disk.

The reader app read this text through a capability:
reader.caps lists "file /apps/readme.txt", the host
checked the path against its policy (read-only, under
/apps) and installed a handle for it.

The reader can read this file and nothing else. It
cannot list the directory, open other files, write,
or reach the network.

Edit /apps/<app>.caps on the disk to change what an
app may do. No rebuild is needed.
