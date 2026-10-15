#include <stdint.h>

#include "minemu/console.h"
#include "minemu/trace.h"
#include "minemu/trap.h"

/*
 * There is no process to kill yet, so a fault stops the machine. The trace
 * value identifies the exception for tests; the console line is for people.
 */
#define TRACE_FAULT_UNDEFINED UINT32_C(0x0fa17004)
#define TRACE_FAULT_PREFETCH_ABORT UINT32_C(0x0fa17002)
#define TRACE_FAULT_DATA_ABORT UINT32_C(0x0fa17001)

static void report_fault(const char *kind, uint32_t trace,
                         const struct minemu_trap_frame *frame) {
    minemu_trace_event(trace);
    minemu_printf("\nminimum: %s at pc %p (spsr %x, dfsr %x, dfar %p)\n", kind,
                  (void *)(uintptr_t)frame->fault_pc, (unsigned)frame->spsr,
                  (unsigned)frame->dfsr, (void *)(uintptr_t)frame->dfar);
    minemu_fail_stop();
}

void minemu_undefined_dispatch(struct minemu_trap_frame *frame) {
    report_fault("undefined instruction", TRACE_FAULT_UNDEFINED, frame);
}

void minemu_abort_dispatch(struct minemu_trap_frame *frame) {
    if (frame->exception_id == MINEMU_EXCEPTION_PREFETCH_ABORT) {
        report_fault("prefetch abort", TRACE_FAULT_PREFETCH_ABORT, frame);
    }
    report_fault("data abort", TRACE_FAULT_DATA_ABORT, frame);
}
