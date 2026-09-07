#include "decl.h"
#include <microkit.h>






void init(void) {
    // initialise the filesystem
}

void notified(microkit_channel ch) {
    // do nothing
}

seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo) {
    // check syscall.
    // do the thing and return.
    int syscall_no = microkit_mr_get(0);
    switch (syscall_no)
    {
        case 0:
            long ramfs_fstatat();
            break;
        case 1:
            long ramfs_readlinkat();
            break;
        case 2:
            long ramfs_openat();
            break;
        case 3:
            long ramfs_lseek();
            break;
        case 4:
            long ramfs_mkdirat();
            break;
        case 5:
            long ramfs_unlinkat();
            break;
        case 6:
            long ramfs_write();
            break;
        case 7:
            long ramfs_read();
            break;
        case 8:
            long ramfs_close();
            break;
        case 9:
            long ramfs_dup3();
            break;
        case 10:
            long ramfs_fstat();
            break;
        default:
            sddf_dprintf("unimplemented syscall!\n");
            break;
    }
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo){
    // not required.
    return seL4_False;
}