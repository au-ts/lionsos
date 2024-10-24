#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include <libvmm/guest.h>
#include <libvmm/virq.h>
#include <libvmm/util/util.h>
#include <libvmm/arch/aarch64/linux.h>
#include <libvmm/arch/aarch64/fault.h>

// Emulate memory read and writes from the guest at the vaddr in VMM's address space
bool emulate_memory(uintptr_t vaddr, size_t fsr, seL4_UserContext *regs) {
    if (fault_is_read(fsr)) {
        uint64_t mask = fault_get_data_mask(vaddr, fsr);
        // LOG_VMM("emulate_memory(): emulated read at offset 0x%x, pc: 0x%lx, mask: 0x%lx, fsr 0x%lx\n", vaddr, regs->pc, mask, fsr);
        switch (mask) {
            case 0x000000ff: {
                uint8_t *target_vaddr = (uint8_t *) vaddr;
                asm volatile("" : : : "memory");
                uint64_t data = *target_vaddr;
                asm volatile("" : : : "memory");
                fault_emulate_write(regs, vaddr, fsr, data);
                break;
            }
            case 0x0000ffff: {
                uint16_t *target_vaddr = (uint16_t *) vaddr;
                asm volatile("" : : : "memory");
                uint64_t data = *target_vaddr;
                asm volatile("" : : : "memory");
                fault_emulate_write(regs, vaddr, fsr, data);
                break;
            }
            case 0xffffffff: {
                uint32_t *target_vaddr = (uint32_t *) vaddr;
                asm volatile("" : : : "memory");
                uint64_t data = *target_vaddr;
                asm volatile("" : : : "memory");
                fault_emulate_write(regs, vaddr, fsr, data);
                break;
            }
            case ~((uint64_t) 0): {
                uint64_t *target_vaddr = (uint64_t *) vaddr;
                asm volatile("" : : : "memory");
                uint64_t data = *target_vaddr;
                asm volatile("" : : : "memory");
                fault_emulate_write(regs, vaddr, fsr, data);
                break;
            }
            default:
                LOG_VMM_ERR("emulate_memory(): unaligned write at vaddr 0x%x\n", vaddr);
                return false;
        }
    } else {
        uint64_t mask = fault_get_data_mask(vaddr, fsr);
        // LOG_VMM("emulate_memory(): emulated write at offset 0x%x, pc: 0x%lx, mask: 0x%lx, fsr 0x%lx\n", vaddr, regs->pc, mask, fsr);

        uint64_t data = fault_get_data(regs, fsr);
        switch (mask) {
            case 0x000000ff: {
                uint8_t *target_vaddr = (uint8_t *) vaddr;
                asm volatile("" : : : "memory");
                *target_vaddr = (uint8_t) (data & mask);
                asm volatile("" : : : "memory");
                break;
            }
            case 0x0000ffff: {
                uint16_t *target_vaddr = (uint16_t *) vaddr;
                asm volatile("" : : : "memory");
                *target_vaddr = (uint16_t) (data & mask);
                asm volatile("" : : : "memory");
                break;
            }
            case 0xffffffff: {
                uint32_t *target_vaddr = (uint32_t *) vaddr;
                asm volatile("" : : : "memory");
                *target_vaddr = (uint32_t) (data & mask);
                asm volatile("" : : : "memory");
                break;
            }
            case ~((uint64_t) 0): {
                uint64_t *target_vaddr = (uint64_t *) vaddr;
                asm volatile("" : : : "memory");
                *target_vaddr = (uint64_t) (data & mask);
                asm volatile("" : : : "memory");
                break;
            }
            default:
                LOG_VMM_ERR("emulate_memory(): unaligned read at vaddr 0x%x\n", vaddr);
                return false;
        }
    }

    return true;
}
