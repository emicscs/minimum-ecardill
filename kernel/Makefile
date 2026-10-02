ARM_PREFIX ?= arm-none-eabi-
CC := $(ARM_PREFIX)gcc
AR := $(ARM_PREFIX)ar

BUILD := build
KERNEL := $(BUILD)/minimum-kernel.elf
RUNTIME := $(BUILD)/libminemu_kernel.a

CORE_OBJECTS := \
	$(BUILD)/core/main.o \
	$(BUILD)/core/shell.o

# Strong IRQ entry. These are kernel-link inputs, not members of
# libminemu_kernel.a. exception.o in that archive provides weak
# minemu_irq_trampoline and minemu_irq_dispatch that fail-stop. Once that
# member is pulled (it also defines minemu_fail_stop), the weak definitions
# satisfy vectors.S, so a strong copy that exists only later in the archive
# is never extracted. --gc-sections then keeps the stubs.
# Objects named on the kernel link line override the weak symbols. Leaving
# them out of the archive also lets irq-context-switch supply its own copies.
IRQ_OBJECTS := \
	$(BUILD)/startup/irq.o \
	$(BUILD)/runtime/irq.o

RUNTIME_OBJECTS := \
	$(BUILD)/runtime/block.o \
	$(BUILD)/runtime/console.o \
	$(BUILD)/runtime/exception.o \
	$(BUILD)/runtime/memory.o \
	$(BUILD)/runtime/trace.o \
	$(BUILD)/runtime/uart.o \
	$(BUILD)/startup/boot.o \
	$(BUILD)/startup/vectors.o

OBJECTS := $(CORE_OBJECTS) $(IRQ_OBJECTS) $(RUNTIME_OBJECTS)
DEPENDENCIES := $(OBJECTS:.o=.d)

EXAMPLES := \
	mmio-basics \
	svc-context-switch \
	irq-context-switch

ARCH_FLAGS := -mcpu=cortex-a9 -marm -mfloat-abi=soft
CPPFLAGS := -Iinclude
CFLAGS := $(ARCH_FLAGS) \
	-ffreestanding \
	-fno-builtin \
	-fdata-sections \
	-ffunction-sections \
	-fno-stack-protector \
	-std=c11 \
	-Wall \
	-Wextra \
	-Werror \
	-MMD \
	-MP

ASFLAGS := $(ARCH_FLAGS) \
	-ffreestanding \
	-MMD \
	-MP

LDFLAGS := $(ARCH_FLAGS) \
	-nostdlib \
	-Wl,-T,linker/kernel.ld \
	-Wl,--gc-sections \
	-Wl,-u,minemu_bootstrap \
	-Wl,-u,minemu_irq_trampoline \
	-Wl,-Map,$(BUILD)/minimum-kernel.map

LIBGCC := $(shell $(CC) $(ARCH_FLAGS) -print-libgcc-file-name)

.PHONY: all runtime examples clean $(EXAMPLES)

all: $(KERNEL)

runtime: $(RUNTIME)

examples: $(EXAMPLES)

$(EXAMPLES):
	$(MAKE) -C examples/$@ all

$(RUNTIME): $(RUNTIME_OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(KERNEL): $(CORE_OBJECTS) $(IRQ_OBJECTS) $(RUNTIME) linker/kernel.ld
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(CORE_OBJECTS) $(IRQ_OBJECTS) $(RUNTIME) $(LIBGCC) -o $@

$(BUILD)/core/%.o: src/core/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/runtime/%.o: src/runtime/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/startup/%.o: src/startup/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(ASFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
	@for example in $(EXAMPLES); do $(MAKE) -C examples/$$example clean; done

-include $(DEPENDENCIES)
