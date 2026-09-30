#include "minemu/console.h"

#include "minemu/uart.h"

#include <stddef.h>
#include <stdint.h>

void minemu_putc(char byte) {
    minemu_uart_putc(MINEMU_UART0, (uint8_t)byte);
}

void minemu_puts(const char *text) {
    minemu_uart_puts(MINEMU_UART0, text);
}

static int emit_unsigned(uint64_t value, unsigned base, int uppercase) {
    char digits[20];
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    unsigned count = 0;
    int written = 0;

    do {
        digits[count++] = alphabet[value % base];
        value /= base;
    } while (value != 0U && count < sizeof(digits));

    while (count > 0U) {
        minemu_putc(digits[--count]);
        ++written;
    }
    return written;
}

static int emit_signed(int64_t value) {
    uint64_t magnitude;
    int written = 0;

    if (value < 0) {
        minemu_putc('-');
        written = 1;
        magnitude = (uint64_t)0 - (uint64_t)value;
    } else {
        magnitude = (uint64_t)value;
    }
    return written + emit_unsigned(magnitude, 10U, 0);
}

static int emit_pointer(uintptr_t value) {
    int written = 0;

    minemu_putc('0');
    minemu_putc('x');
    written = 2;
    for (int shift = 28; shift >= 0; shift -= 4) {
        unsigned digit = ((uint32_t)value >> shift) & 0xfu;
        minemu_putc("0123456789abcdef"[digit]);
        ++written;
    }
    return written;
}

static int emit_string(const char *text) {
    int written = 0;

    if (text == NULL) {
        text = "(null)";
    }
    while (*text != '\0') {
        minemu_putc(*text);
        ++text;
        ++written;
    }
    return written;
}

static void skip_format_decorations(const char **fmt) {
    const char *cursor = *fmt;

    while (*cursor == '-' || *cursor == '+' || *cursor == ' ' || *cursor == '#' ||
           *cursor == '0') {
        ++cursor;
    }
    while (*cursor >= '0' && *cursor <= '9') {
        ++cursor;
    }
    if (*cursor == '.') {
        ++cursor;
        while (*cursor >= '0' && *cursor <= '9') {
            ++cursor;
        }
    }
    *fmt = cursor;
}

/* 0 = int, 1 = long/size_t, 2 = long long. h and hh stay at 0: they promote. */
static int parse_length(const char **fmt) {
    const char *cursor = *fmt;
    int length = 0;

    if (*cursor == 'l') {
        ++cursor;
        if (*cursor == 'l') {
            length = 2;
            ++cursor;
        } else {
            length = 1;
        }
    } else if (*cursor == 'h') {
        ++cursor;
        if (*cursor == 'h') {
            ++cursor;
        }
    } else if (*cursor == 'z' || *cursor == 't') {
        length = 1;
        ++cursor;
    }
    *fmt = cursor;
    return length;
}

static int64_t take_signed(va_list *ap, int length) {
    if (length >= 2) {
        return va_arg(*ap, long long);
    }
    if (length == 1) {
        return va_arg(*ap, long);
    }
    return va_arg(*ap, int);
}

static uint64_t take_unsigned(va_list *ap, int length) {
    if (length >= 2) {
        return va_arg(*ap, unsigned long long);
    }
    if (length == 1) {
        return va_arg(*ap, unsigned long);
    }
    return va_arg(*ap, unsigned int);
}

int minemu_vprintf(const char *fmt, va_list ap) {
    int written = 0;

    if (fmt == NULL) {
        return 0;
    }

    while (*fmt != '\0') {
        char spec;
        int length;

        if (*fmt != '%') {
            minemu_putc(*fmt);
            ++fmt;
            ++written;
            continue;
        }

        ++fmt;
        if (*fmt == '\0') {
            minemu_putc('%');
            ++written;
            break;
        }
        if (*fmt == '%') {
            minemu_putc('%');
            ++fmt;
            ++written;
            continue;
        }

        skip_format_decorations(&fmt);
        length = parse_length(&fmt);
        spec = *fmt;
        if (spec == '\0') {
            minemu_putc('%');
            ++written;
            break;
        }
        ++fmt;

        switch (spec) {
        case 'c':
            minemu_putc((char)va_arg(ap, int));
            ++written;
            break;
        case 's':
            written += emit_string(va_arg(ap, const char *));
            break;
        case 'd':
        case 'i':
            written += emit_signed(take_signed(&ap, length));
            break;
        case 'u':
            written += emit_unsigned(take_unsigned(&ap, length), 10U, 0);
            break;
        case 'x':
            written += emit_unsigned(take_unsigned(&ap, length), 16U, 0);
            break;
        case 'X':
            written += emit_unsigned(take_unsigned(&ap, length), 16U, 1);
            break;
        case 'p':
            written += emit_pointer((uintptr_t)va_arg(ap, void *));
            break;
        default:
            minemu_putc('%');
            minemu_putc(spec);
            written += 2;
            break;
        }
    }

    return written;
}

int minemu_printf(const char *fmt, ...) {
    va_list ap;
    int written;

    va_start(ap, fmt);
    written = minemu_vprintf(fmt, ap);
    va_end(ap);
    return written;
}
