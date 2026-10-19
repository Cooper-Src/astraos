#ifndef ASTRAOS_FRAMEBUFFER_H
#define ASTRAOS_FRAMEBUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_info.h"

/* Uses UEFI GOP metadata captured by the loader. It calls no firmware APIs. */
void astra_framebuffer_init(const AstraBootInfo *boot_info);
bool astra_framebuffer_is_active(void);
void astra_framebuffer_clear(void);
void astra_framebuffer_put_byte(uint8_t value);

/* Small bounded drawing API used by the in-kernel compositor. */
bool astra_framebuffer_get_dimensions(uint32_t *width_out, uint32_t *height_out);
void astra_framebuffer_set_console_enabled(bool enabled);
void astra_framebuffer_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t rgb24);
void astra_framebuffer_draw_text(uint32_t x, uint32_t y, const char *text, size_t length, uint32_t rgb24);

#endif
