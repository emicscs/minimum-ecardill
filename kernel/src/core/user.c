#include "minemu/user.h"

#include "minemu/console.h"
#include "minemu/platform.h"

#define BOOTSTRAP_PAGE_DIRECTORY_PADDR UINT32_C(0x40010000)
#define MAX_USER_IMAGE_PAGES 16U
#define SYSTEM_ROM_TABLE_COUNT 4U
#define PAGE_TABLE_ENTRIES 1024U

/*
 * Page tables and user pages live in free RAM past the kernel image rather
 * than in .bss: the Boot ROM clears BSS one volatile byte at a time, and
 * these 100 KB alone cost more boot time than the tests allow. Every page
 * here is zeroed by the loader before use.
 */
#define USER_MEMORY_PADDR UINT32_C(0x40100000)

struct user_memory {
    uint32_t system_rom_tables[SYSTEM_ROM_TABLE_COUNT][PAGE_TABLE_ENTRIES];
    uint32_t page_table[PAGE_TABLE_ENTRIES];
    uint8_t stack_pages[MINEMU_USER_STACK_PAGES][MINEMU_PAGE_SIZE];
    uint8_t image_pages[MAX_USER_IMAGE_PAGES][MINEMU_PAGE_SIZE];
};

extern char __minemu_und_stack_top[];

#define USER_MEMORY                                                                        \
    ((struct user_memory *)(uintptr_t)(USER_MEMORY_PADDR - MINEMU_RAM_BASE +               \
                                       MINEMU_KERNEL_DIRECT_BASE))
#define system_rom_tables (USER_MEMORY->system_rom_tables)
#define user_page_table (USER_MEMORY->page_table)
#define user_stack_pages (USER_MEMORY->stack_pages)
#define user_image_pages (USER_MEMORY->image_pages)

static uint32_t user_page_vaddrs[MAX_USER_IMAGE_PAGES];
static uint32_t user_page_flags[MAX_USER_IMAGE_PAGES];
static size_t user_page_count;

/*
 * Loading runs before user entry and inside the tests' tick budgets, so
 * page-sized clears and copies go a word at a time whenever alignment allows.
 */
static bool word_aligned(uintptr_t value) {
    return (value & 3U) == 0U;
}

static void zero_bytes(void *destination, size_t length) {
    if (word_aligned((uintptr_t)destination | length)) {
        uint32_t *words = destination;
        for (size_t index = 0; index < length / 4U; ++index) {
            words[index] = 0;
        }
        return;
    }
    uint8_t *bytes = destination;
    for (size_t index = 0; index < length; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t length) {
    if (word_aligned((uintptr_t)destination | (uintptr_t)source | length)) {
        uint32_t *out = destination;
        const uint32_t *in = source;
        for (size_t index = 0; index < length / 4U; ++index) {
            out[index] = in[index];
        }
        return;
    }
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t index = 0; index < length; ++index) {
        out[index] = in[index];
    }
}

static bool bytes_equal(const uint8_t *left, const char *right, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        if (left[index] != (uint8_t)right[index]) {
            return false;
        }
    }
    return true;
}

static size_t string_length(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool image_range_valid(uint32_t offset, uint32_t length, uint32_t image_size) {
    return offset <= image_size && length <= image_size - offset;
}

static bool image_records_valid(uint32_t offset, uint32_t count, uint32_t image_size) {
    return offset <= image_size && count <= ((image_size - offset) >> 5);
}

static uint32_t kernel_vaddr_to_paddr(const void *address) {
    return (uint32_t)(uintptr_t)address - MINEMU_KERNEL_DIRECT_BASE + MINEMU_RAM_BASE;
}

static uint8_t *paddr_to_kernel_vaddr(uint32_t paddr) {
    return (uint8_t *)(uintptr_t)(paddr - MINEMU_RAM_BASE + MINEMU_KERNEL_DIRECT_BASE);
}

static volatile uint32_t *active_page_directory(void) {
    return (volatile uint32_t *)(uintptr_t)(MINEMU_KERNEL_DIRECT_BASE +
                                            BOOTSTRAP_PAGE_DIRECTORY_PADDR - MINEMU_RAM_BASE);
}

/* Kernel-only, read-only view of the packaged image at its physical address. */
static bool map_system_rom(const struct minemu_boot_info *boot_info) {
    if (boot_info->system_rom_base != MINEMU_SYSTEM_ROM_BASE || boot_info->image_size == 0 ||
        boot_info->image_size > MINEMU_SYSTEM_ROM_SIZE) {
        return false;
    }

    uint32_t page_count = (boot_info->image_size + MINEMU_PAGE_SIZE - 1U) / MINEMU_PAGE_SIZE;
    uint32_t table_count = (page_count + PAGE_TABLE_ENTRIES - 1U) / PAGE_TABLE_ENTRIES;
    if (table_count > SYSTEM_ROM_TABLE_COUNT) {
        return false;
    }

    volatile uint32_t *directory = active_page_directory();
    for (uint32_t table = 0; table < table_count; ++table) {
        zero_bytes(system_rom_tables[table], sizeof(system_rom_tables[table]));
        uint32_t remaining = page_count - table * PAGE_TABLE_ENTRIES;
        uint32_t entries = remaining < PAGE_TABLE_ENTRIES ? remaining : PAGE_TABLE_ENTRIES;
        for (uint32_t index = 0; index < entries; ++index) {
            uint32_t page = table * PAGE_TABLE_ENTRIES + index;
            system_rom_tables[table][index] =
                (boot_info->system_rom_base + page * MINEMU_PAGE_SIZE) | MINEMU_PTE_VALID |
                MINEMU_PTE_READABLE;
        }
        uint32_t directory_index = (boot_info->system_rom_base >> 22) + table;
        directory[directory_index] =
            kernel_vaddr_to_paddr(system_rom_tables[table]) | MINEMU_PDE_VALID;
    }
    minemu_mmu_tlbiall();
    return true;
}

static const struct minemu_module_record *find_module(const struct minemu_boot_info *boot_info,
                                                       const char *name) {
    const uint8_t *image = (const uint8_t *)(uintptr_t)boot_info->system_rom_base;
    size_t name_length = string_length(name);
    if (!image_records_valid(boot_info->module_table_offset, boot_info->module_count,
                             boot_info->image_size)) {
        return NULL;
    }

    const struct minemu_module_record *modules =
        (const struct minemu_module_record *)(image + boot_info->module_table_offset);
    for (uint32_t index = 0; index < boot_info->module_count; ++index) {
        const struct minemu_module_record *module = &modules[index];
        if (module->name_length == name_length &&
            image_range_valid(module->name_offset, module->name_length,
                              boot_info->image_size) &&
            bytes_equal(image + module->name_offset, name, name_length)) {
            return module;
        }
    }
    return NULL;
}

static uint32_t pte_flags_from_segment(uint32_t segment_flags) {
    uint32_t pte_flags = 0;
    if ((segment_flags & MINEMU_SEGMENT_READABLE) != 0) {
        pte_flags |= MINEMU_PTE_READABLE;
    }
    if ((segment_flags & MINEMU_SEGMENT_WRITABLE) != 0) {
        pte_flags |= MINEMU_PTE_WRITABLE;
    }
    if ((segment_flags & MINEMU_SEGMENT_EXECUTABLE) != 0) {
        pte_flags |= MINEMU_PTE_EXECUTABLE;
    }
    return pte_flags;
}

/*
 * Two segments may share a page only if they ask for the same permissions;
 * otherwise one of them would silently gain the other's rights.
 */
static int user_page_index(uint32_t page_vaddr, uint32_t flags, bool create) {
    for (size_t index = 0; index < user_page_count; ++index) {
        if (user_page_vaddrs[index] == page_vaddr) {
            if (create && user_page_flags[index] != flags) {
                return -1;
            }
            return (int)index;
        }
    }
    if (!create || user_page_count == MAX_USER_IMAGE_PAGES) {
        return -1;
    }
    size_t index = user_page_count++;
    user_page_vaddrs[index] = page_vaddr;
    user_page_flags[index] = flags;
    zero_bytes(user_image_pages[index], MINEMU_PAGE_SIZE);
    return (int)index;
}

static bool prepare_image_range(uint32_t vaddr, uint32_t length, uint32_t flags) {
    uint32_t end = vaddr + length;
    for (uint32_t page = vaddr & MINEMU_PTE_PAGE_MASK; page < end; page += MINEMU_PAGE_SIZE) {
        if (user_page_index(page, flags, true) < 0) {
            return false;
        }
    }
    return true;
}

static bool write_image_range(uint32_t vaddr, const uint8_t *source, uint32_t length) {
    while (length != 0) {
        uint32_t page_vaddr = vaddr & MINEMU_PTE_PAGE_MASK;
        int index = user_page_index(page_vaddr, 0, false);
        if (index < 0) {
            return false;
        }
        uint32_t offset = vaddr - page_vaddr;
        uint32_t chunk = MINEMU_PAGE_SIZE - offset;
        if (chunk > length) {
            chunk = length;
        }
        copy_bytes(&user_image_pages[index][offset], source, chunk);
        source += chunk;
        vaddr += chunk;
        length -= chunk;
    }
    return true;
}

static bool segment_valid(const struct minemu_boot_info *boot_info,
                          const struct minemu_module_segment *segment) {
    uint32_t allowed = MINEMU_SEGMENT_READABLE | MINEMU_SEGMENT_WRITABLE |
                       MINEMU_SEGMENT_EXECUTABLE;
    return segment->memory_size != 0 && segment->file_size <= segment->memory_size &&
           (segment->flags & ~allowed) == 0 &&
           ((segment->flags & MINEMU_SEGMENT_EXECUTABLE) == 0 ||
            (segment->flags & MINEMU_SEGMENT_READABLE) != 0) &&
           segment->virtual_address >= MINEMU_USER_BASE &&
           segment->virtual_address < MINEMU_USER_STACK_BOTTOM &&
           segment->memory_size <= MINEMU_USER_STACK_BOTTOM - segment->virtual_address &&
           image_range_valid(segment->data_offset, segment->file_size, boot_info->image_size);
}

static bool load_module(const struct minemu_boot_info *boot_info,
                        const struct minemu_module_record *module) {
    const uint8_t *image = (const uint8_t *)(uintptr_t)boot_info->system_rom_base;
    if (module->segment_count == 0 ||
        !image_records_valid(module->segment_table_offset, module->segment_count,
                             boot_info->image_size) ||
        (module->entry_vaddr & 3U) != 0) {
        minemu_printf("minimum: load: bad segment table (count %u) or entry %p\n",
                      (unsigned)module->segment_count,
                      (void *)(uintptr_t)module->entry_vaddr);
        return false;
    }

    const struct minemu_module_segment *segments =
        (const struct minemu_module_segment *)(image + module->segment_table_offset);
    bool entry_is_executable = false;
    zero_bytes(user_page_table, sizeof(user_page_table));
    zero_bytes(user_stack_pages, sizeof(user_stack_pages));
    user_page_count = 0;

    for (uint32_t index = 0; index < module->segment_count; ++index) {
        const struct minemu_module_segment *segment = &segments[index];
        if (!segment_valid(boot_info, segment)) {
            minemu_printf("minimum: load: segment %u rejected: vaddr %p filesz %x memsz %x "
                          "flags %x\n",
                          (unsigned)index, (void *)(uintptr_t)segment->virtual_address,
                          (unsigned)segment->file_size, (unsigned)segment->memory_size,
                          (unsigned)segment->flags);
            return false;
        }
        /* Fresh pages are zeroed, which covers BSS past file_size. */
        if (!prepare_image_range(segment->virtual_address, segment->memory_size,
                                 pte_flags_from_segment(segment->flags)) ||
            !write_image_range(segment->virtual_address, image + segment->data_offset,
                               segment->file_size)) {
            minemu_printf("minimum: load: segment %u pages: conflicting permissions "
                          "or more than %u pages\n",
                          (unsigned)index, MAX_USER_IMAGE_PAGES);
            return false;
        }
        if ((segment->flags & MINEMU_SEGMENT_EXECUTABLE) != 0 && segment->file_size >= 4U &&
            module->entry_vaddr >= segment->virtual_address &&
            module->entry_vaddr - segment->virtual_address <= segment->file_size - 4U) {
            entry_is_executable = true;
        }
    }
    if (!entry_is_executable) {
        minemu_printf("minimum: load: entry %p is not in an executable segment\n",
                      (void *)(uintptr_t)module->entry_vaddr);
        return false;
    }

    for (size_t index = 0; index < user_page_count; ++index) {
        uint32_t table_index = (user_page_vaddrs[index] >> 12) & (PAGE_TABLE_ENTRIES - 1U);
        user_page_table[table_index] = kernel_vaddr_to_paddr(user_image_pages[index]) |
                                       MINEMU_PTE_VALID | MINEMU_PTE_USER |
                                       user_page_flags[index];
    }
    /* Read/write, never executable. The page below the stack stays unmapped. */
    for (uint32_t page = 0; page < MINEMU_USER_STACK_PAGES; ++page) {
        uint32_t vaddr = MINEMU_USER_STACK_BOTTOM + page * MINEMU_PAGE_SIZE;
        user_page_table[(vaddr >> 12) & (PAGE_TABLE_ENTRIES - 1U)] =
            kernel_vaddr_to_paddr(user_stack_pages[page]) | MINEMU_PTE_VALID |
            MINEMU_PTE_USER | MINEMU_PTE_READABLE | MINEMU_PTE_WRITABLE;
    }

    volatile uint32_t *directory = active_page_directory();
    directory[MINEMU_USER_BASE >> 22] = kernel_vaddr_to_paddr(user_page_table) | MINEMU_PDE_VALID;
    minemu_mmu_tlbiall();
    return true;
}

bool minemu_user_load(const struct minemu_boot_info *boot_info, const char *name,
                      uint32_t *entry_out) {
    if ((uintptr_t)__minemu_und_stack_top > (uintptr_t)USER_MEMORY) {
        minemu_printf("minimum: load: kernel image overlaps user memory at %p\n",
                      (void *)USER_MEMORY);
        return false;
    }
    if (!map_system_rom(boot_info)) {
        minemu_printf("minimum: load: cannot map system ROM (image size %x)\n",
                      (unsigned)boot_info->image_size);
        return false;
    }
    const struct minemu_module_record *module = find_module(boot_info, name);
    if (module == NULL) {
        minemu_printf("minimum: load: no module named %s among %u\n", name,
                      (unsigned)boot_info->module_count);
        return false;
    }
    if (!load_module(boot_info, module)) {
        return false;
    }
    *entry_out = module->entry_vaddr;
    return true;
}

static uint32_t user_pte(uint32_t vaddr) {
    if (vaddr < MINEMU_USER_BASE || vaddr >= MINEMU_USER_END) {
        return 0;
    }
    return user_page_table[(vaddr >> 12) & (PAGE_TABLE_ENTRIES - 1U)];
}

bool minemu_user_range_ok(uint32_t vaddr, uint32_t length, uint32_t required_pte_flags) {
    if (length == 0) {
        return true;
    }
    if (vaddr > UINT32_MAX - (length - 1U)) {
        return false;
    }
    uint32_t required = MINEMU_PTE_VALID | MINEMU_PTE_USER | required_pte_flags;
    uint32_t last_page = (vaddr + (length - 1U)) & MINEMU_PTE_PAGE_MASK;
    for (uint32_t page = vaddr & MINEMU_PTE_PAGE_MASK;; page += MINEMU_PAGE_SIZE) {
        if ((user_pte(page) & required) != required) {
            return false;
        }
        if (page == last_page) {
            return true;
        }
    }
}

/* Caller has validated the page. */
static uint8_t *user_kernel_alias(uint32_t vaddr) {
    uint32_t pte = user_pte(vaddr);
    return paddr_to_kernel_vaddr(pte & MINEMU_PTE_PAGE_MASK) + (vaddr & ~MINEMU_PTE_PAGE_MASK);
}

bool minemu_user_copy_from(void *destination, uint32_t vaddr, uint32_t length,
                           uint32_t required_pte_flags) {
    if (!minemu_user_range_ok(vaddr, length, required_pte_flags)) {
        return false;
    }
    uint8_t *out = destination;
    while (length != 0) {
        uint32_t chunk = MINEMU_PAGE_SIZE - (vaddr & ~MINEMU_PTE_PAGE_MASK);
        if (chunk > length) {
            chunk = length;
        }
        copy_bytes(out, user_kernel_alias(vaddr), chunk);
        out += chunk;
        vaddr += chunk;
        length -= chunk;
    }
    return true;
}

bool minemu_user_copy_to(uint32_t vaddr, const void *source, uint32_t length) {
    if (!minemu_user_range_ok(vaddr, length, MINEMU_PTE_WRITABLE)) {
        return false;
    }
    const uint8_t *in = source;
    while (length != 0) {
        uint32_t chunk = MINEMU_PAGE_SIZE - (vaddr & ~MINEMU_PTE_PAGE_MASK);
        if (chunk > length) {
            chunk = length;
        }
        copy_bytes(user_kernel_alias(vaddr), in, chunk);
        in += chunk;
        vaddr += chunk;
        length -= chunk;
    }
    return true;
}
