#ifndef ASTRAOS_FRAMEBUFFER_H
#define ASTRAOS_FRAMEBUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_info.h"

/* Uses UEFI GOP metadata captured by the loader. It calls no firmware APIs. */
void astra_framebuffer_init(const AstraBootInfo *boot_info);
bool astra_framebuffer_is_active(void);
uint32_t astra_framebuffer_width(void);
uint32_t astra_framebuffer_height(void);
void astra_framebuffer_set_console_enabled(bool enabled);
void astra_framebuffer_clear(void);
void astra_framebuffer_put_byte(uint8_t value);

/* Immediate-mode RGB drawing. Colors are 0xRRGGBB and rectangles are clipped. */
void astra_framebuffer_fill_rect(
    uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t rgb
);
void astra_framebuffer_draw_text(
    uint32_t x,
    uint32_t y,
    const char *text,
    size_t length,
    uint32_t foreground_rgb,
    uint32_t background_rgb
);

#endif
