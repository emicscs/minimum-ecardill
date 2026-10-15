#ifndef MINEMU_USER_H
#define MINEMU_USER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "minemu/boot.h"
#include "minemu/mmu.h"

/*
 * One user address space, held in page-directory slot 1 of the active
 * directory: virtual 0x00400000 up to 0x00800000. Module segments load at
 * 0x00400000 (user/common/user.ld) and the stack occupies the top pages of
 * the slot. Every user page is a private copy in kernel RAM, so kernel and
 * MMIO mappings never become user-accessible.
 */
#define MINEMU_USER_BASE UINT32_C(0x00400000)
#define MINEMU_USER_END UINT32_C(0x00800000)
#define MINEMU_USER_STACK_PAGES 4U
#define MINEMU_USER_STACK_TOP MINEMU_USER_END
#define MINEMU_USER_STACK_BOTTOM \
    (MINEMU_USER_STACK_TOP - MINEMU_USER_STACK_PAGES * MINEMU_PAGE_SIZE)

/*
 * Maps the packaged image read-only into the kernel, finds the module named
 * name, and builds its address space. Returns false, with nothing entered,
 * if the image or module is malformed or does not fit.
 */
bool minemu_user_load(const struct minemu_boot_info *boot_info, const char *name,
                      uint32_t *entry_out);

/* Drops to USR mode at entry with SP = stack_top and IRQ unmasked. */
void minemu_enter_user(uint32_t entry, uint32_t stack_top) __attribute__((noreturn));

/*
 * True when every page touched by [vaddr, vaddr + length) is a valid user
 * page carrying all of required_pte_flags (MINEMU_PTE_READABLE, _WRITABLE,
 * _EXECUTABLE). A zero length is always valid. Wraparound is rejected.
 */
bool minemu_user_range_ok(uint32_t vaddr, uint32_t length, uint32_t required_pte_flags);

/*
 * Copies validate the whole range against required flags before moving any
 * byte, then copy through the kernel's own mapping of the backing pages.
 * They return false without touching memory if validation fails.
 */
bool minemu_user_copy_from(void *destination, uint32_t vaddr, uint32_t length,
                           uint32_t required_pte_flags);
bool minemu_user_copy_to(uint32_t vaddr, const void *source, uint32_t length);

#endif
