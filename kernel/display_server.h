#ifndef ASTRAOS_DISPLAY_SERVER_H
#define ASTRAOS_DISPLAY_SERVER_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*AstraDisplayRenderCallback)(
    uint32_t content_width,
    uint32_t content_height,
    void *context
);

/* A small, retained-mode window manager and software framebuffer compositor. */
bool astra_display_server_initialize(void);
bool astra_display_server_is_active(void);
uint32_t astra_display_server_width(void);
uint32_t astra_display_server_height(void);
uint32_t astra_display_server_window_count(void);

bool astra_display_window_create(
    const char *title,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    AstraDisplayRenderCallback render,
    void *context,
    uint32_t *window_id_out
);
bool astra_display_window_set_visible(uint32_t window_id, bool visible);
bool astra_display_window_raise(uint32_t window_id);
bool astra_display_window_close(uint32_t window_id);
void astra_display_server_present(void);

/* Drawing calls are clipped to the content area of the window being rendered. */
void astra_display_draw_rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t rgb
);
void astra_display_draw_text(
    uint32_t x,
    uint32_t y,
    const char *text,
    uint32_t foreground_rgb,
    uint32_t background_rgb
);

#endif
