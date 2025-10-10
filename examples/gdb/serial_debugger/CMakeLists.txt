add_executable(debugger.elf debugger.c)
target_link_libraries(debugger.elf gdb libco sddf)