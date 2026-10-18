#ifndef ASTRA_CONSOLE_H
#define ASTRA_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

struct limine_dtb_response;
struct limine_framebuffer_response;

/* Initializes the optional framebuffer and a PL011 UART described by the device tree. */
void console_init(struct limine_framebuffer_response *framebuffer_response,
                  struct limine_dtb_response *dtb_response, uint64_t hhdm_offset);

/* Writes ASCII text to each available console. */
void console_write(const char *text);
void console_write_char(char character);
void console_clear(void);

/* Returns an ASCII character, or -1 if no serial input is available. */
int console_read_char(void);
bool console_has_input(void);

/* Writes an unsigned decimal integer without a hosted C library. */
void console_write_u64(uint64_t value);

#endif
