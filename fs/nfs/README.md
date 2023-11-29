* Issues

- os.listdir() on some directories causes a crash (IP=NULL), e.g. os.listdir('content/images').
- need a proper asynchronous client library
- need examples in the README on how to use it
- dependency on libgcc
- libnfs doesn't work with clang
- libnfs doesn't build on macos due to unsupported flag
- some stuff not implemented, e.g. file modes
- using musl instead of our own libc
- musl calls into gettime() to get real-time clock, but we don't have an rtc
- unsure of memory lifetimes of libnfs, i.e. how robust is our heap management to long-running sessions?
- unsure of socket lifetimes, i.e. do we need to do anything extra to handle disconnects/reconnects etc?
