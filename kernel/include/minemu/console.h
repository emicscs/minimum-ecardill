#ifndef MINEMU_CONSOLE_H
#define MINEMU_CONSOLE_H

#include <stdarg.h>

/*
 * Kernel console bound to UART0.
 *
 * minemu_puts and minemu_printf do not append a newline. Include '\n' in the
 * text when a line break is required.
 *
 * Supported conversions: %%, %c, %s, %d, %i, %u, %x, %X, %p.
 * Length modifiers h, hh, l, ll, z, and t are accepted on integer conversions.
 * On this 32-bit ABI, l, z, and t are the same width as int. Literal flags,
 * numeric field width, and numeric precision are accepted and ignored. A '*'
 * width or precision is not supported. %p is printed as 0x followed by eight
 * lowercase hex digits.
 * A NULL %s argument prints "(null)".
 */
void minemu_putc(char byte);
void minemu_puts(const char *text);
int minemu_vprintf(const char *fmt, va_list ap);
int minemu_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
