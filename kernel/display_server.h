#ifndef ASTRAOS_DISPLAY_SERVER_H
#define ASTRAOS_DISPLAY_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    ASTRA_DISPLAY_MAX_WINDOWS = 4,
    ASTRA_DISPLAY_TITLE_CAP = 32
};

typedef struct AstraDisplayWindow AstraDisplayWindow;
typedef void (*AstraDisplayWindowPaint)(
    const AstraDisplayWindow *window,
    void *context
);

struct AstraDisplayWindow {
    uint32_t id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t content_width;
    uint32_t content_height;
    bool visible;
    bool focused;
    char title[ASTRA_DISPLAY_TITLE_CAP];
    AstraDisplayWindowPaint paint;
    void *context;
};

/* Kernel-resident fixed-size window manager and GOP framebuffer compositor. */
bool astra_display_server_initialize(void);
void astra_display_server_shutdown(void);
bool astra_display_server_is_active(void);
uint32_t astra_display_server_width(void);
uint32_t astra_display_server_height(void);
size_t astra_display_server_window_count(void);
const AstraDisplayWindow *astra_display_server_focused_window(void);
bool astra_display_server_open_window(
    const char *title,
    uint32_t width,
    uint32_t height,
    AstraDisplayWindowPaint paint,
    void *context,
    uint32_t *window_id_out
);
bool astra_display_server_close_window(uint32_t window_id);
bool astra_display_server_focus_next(void);
void astra_display_server_render(void);

/* Window-local immediate-mode drawing, clipped to the client content area. */
void astra_display_window_fill(
    const AstraDisplayWindow *window,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t rgb
);
void astra_display_window_text(
    const AstraDisplayWindow *window,
    uint32_t x,
    uint32_t y,
    const char *text,
    size_t length,
    uint32_t foreground_rgb,
    uint32_t background_rgb
);

#endif
