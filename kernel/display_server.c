#include "display_server.h"

#include "framebuffer.h"

#include <stddef.h>
#include <stdint.h>

enum {
    DISPLAY_MAX_WINDOWS = 6u,
    DISPLAY_TITLE_CAP = 32u,
    DISPLAY_TITLE_BAR_HEIGHT = 28u,
    DISPLAY_BORDER = 2u,
    DISPLAY_CONTENT_INSET = 6u,
    DISPLAY_CELL_WIDTH = 8u,
    DISPLAY_CELL_HEIGHT = 16u,
    DISPLAY_MIN_WIDTH = 200u,
    DISPLAY_MIN_HEIGHT = 112u
};

typedef struct AstraDisplayWindow {
    uint32_t id;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    bool visible;
    char title[DISPLAY_TITLE_CAP];
    AstraDisplayRenderCallback render;
    void *context;
} AstraDisplayWindow;

typedef struct AstraDisplayServerState {
    AstraDisplayWindow windows[DISPLAY_MAX_WINDOWS];
    uint32_t window_count;
    uint32_t next_id;
    uint32_t active_id;
    uint32_t width;
    uint32_t height;
    bool active;
} AstraDisplayServerState;

static AstraDisplayServerState g_display;
static AstraDisplayWindow *g_render_window = NULL;
static uint32_t g_render_content_x = 0u;
static uint32_t g_render_content_y = 0u;
static uint32_t g_render_content_width = 0u;
static uint32_t g_render_content_height = 0u;

static uint32_t content_width(const AstraDisplayWindow *window) {
    if (window == NULL || window->width <= DISPLAY_CONTENT_INSET * 2u) {
        return 0u;
    }
    return window->width - DISPLAY_CONTENT_INSET * 2u;
}

static uint32_t content_height(const AstraDisplayWindow *window) {
    if (window == NULL ||
        window->height <= DISPLAY_TITLE_BAR_HEIGHT + DISPLAY_CONTENT_INSET) {
        return 0u;
    }
    return window->height - DISPLAY_TITLE_BAR_HEIGHT - DISPLAY_CONTENT_INSET;
}

static AstraDisplayWindow *find_window(uint32_t id) {
    if (id == 0u) {
        return NULL;
    }
    for (uint32_t i = 0u; i < g_display.window_count; i++) {
        if (g_display.windows[i].id == id) {
            return &g_display.windows[i];
        }
    }
    return NULL;
}

static uint32_t first_visible_window_id(void) {
    for (uint32_t i = g_display.window_count; i > 0u; i--) {
        const AstraDisplayWindow *window = &g_display.windows[i - 1u];
        if (window->visible) {
            return window->id;
        }
    }
    return 0u;
}

bool astra_display_server_initialize(void) {
    uint32_t width = astra_framebuffer_width();
    uint32_t height = astra_framebuffer_height();

    g_display.active = false;
    g_display.window_count = 0u;
    g_display.next_id = 1u;
    g_display.active_id = 0u;
    g_display.width = 0u;
    g_display.height = 0u;
    g_render_window = NULL;

    if (!astra_framebuffer_is_active() || width < 320u || height < 240u) {
        astra_framebuffer_set_console_enabled(true);
        return false;
    }

    g_display.width = width;
    g_display.height = height;
    g_display.active = true;
    astra_framebuffer_set_console_enabled(false);
    return true;
}

bool astra_display_server_is_active(void) {
    return g_display.active;
}

uint32_t astra_display_server_width(void) {
    return g_display.active ? g_display.width : 0u;
}

uint32_t astra_display_server_height(void) {
    return g_display.active ? g_display.height : 0u;
}

uint32_t astra_display_server_window_count(void) {
    return g_display.active ? g_display.window_count : 0u;
}

bool astra_display_window_create(
    const char *title,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    AstraDisplayRenderCallback render,
    void *context,
    uint32_t *window_id_out
) {
    AstraDisplayWindow *window = NULL;
    uint32_t max_title_chars = 0u;
    uint32_t title_length = 0u;

    if (window_id_out != NULL) {
        *window_id_out = 0u;
    }
    if (!g_display.active || title == NULL || render == NULL ||
        window_id_out == NULL || g_display.window_count >= DISPLAY_MAX_WINDOWS ||
        width < DISPLAY_MIN_WIDTH || height < DISPLAY_MIN_HEIGHT ||
        x >= g_display.width || y >= g_display.height ||
        width > g_display.width - x || height > g_display.height - y) {
        return false;
    }

    window = &g_display.windows[g_display.window_count];
    window->id = g_display.next_id++;
    if (window->id == 0u) {
        window->id = g_display.next_id++;
    }
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->visible = true;
    window->render = render;
    window->context = context;

    max_title_chars = (width > 72u ? (width - 72u) / DISPLAY_CELL_WIDTH : 0u);
    if (max_title_chars >= DISPLAY_TITLE_CAP) {
        max_title_chars = DISPLAY_TITLE_CAP - 1u;
    }
    while (title_length < max_title_chars &&
        title[title_length] != '\0') {
        window->title[title_length] = title[title_length];
        title_length++;
    }
    window->title[title_length] = '\0';

    g_display.window_count++;
    g_display.active_id = window->id;
    *window_id_out = window->id;
    return true;
}

bool astra_display_window_set_visible(uint32_t window_id, bool visible) {
    AstraDisplayWindow *window = find_window(window_id);

    if (!g_display.active || window == NULL) {
        return false;
    }
    window->visible = visible;
    if (!visible && g_display.active_id == window_id) {
        g_display.active_id = first_visible_window_id();
    } else if (visible) {
        g_display.active_id = window_id;
    }
    return true;
}

bool astra_display_window_raise(uint32_t window_id) {
    AstraDisplayWindow selected = {0};
    uint32_t index = 0u;

    if (!g_display.active) {
        return false;
    }
    for (; index < g_display.window_count; index++) {
        if (g_display.windows[index].id == window_id) {
            break;
        }
    }
    if (index == g_display.window_count || !g_display.windows[index].visible) {
        return false;
    }

    selected = g_display.windows[index];
    for (uint32_t i = index; i + 1u < g_display.window_count; i++) {
        g_display.windows[i] = g_display.windows[i + 1u];
    }
    g_display.windows[g_display.window_count - 1u] = selected;
    g_display.active_id = window_id;
    return true;
}

bool astra_display_window_close(uint32_t window_id) {
    return astra_display_window_set_visible(window_id, false);
}

void astra_display_draw_rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t rgb
) {
    if (!g_display.active || g_render_window == NULL ||
        x >= g_render_content_width || y >= g_render_content_height ||
        width == 0u || height == 0u) {
        return;
    }
    if (width > g_render_content_width - x) {
        width = g_render_content_width - x;
    }
    if (height > g_render_content_height - y) {
        height = g_render_content_height - y;
    }
    astra_framebuffer_fill_rgb(
        g_render_content_x + x,
        g_render_content_y + y,
        width,
        height,
        rgb
    );
}

void astra_display_draw_text(
    uint32_t x,
    uint32_t y,
    const char *text,
    uint32_t foreground_rgb,
    uint32_t background_rgb
) {
    uint32_t max_cells = 0u;

    if (!g_display.active || g_render_window == NULL || text == NULL ||
        x >= g_render_content_width || y >= g_render_content_height ||
        y > g_render_content_height - DISPLAY_CELL_HEIGHT) {
        return;
    }
    max_cells = (g_render_content_width - x) / DISPLAY_CELL_WIDTH;
    for (uint32_t i = 0u; i < max_cells && text[i] != '\0'; i++) {
        char cell[2] = {text[i], '\0'};
        astra_framebuffer_draw_text(
            g_render_content_x + x + i * DISPLAY_CELL_WIDTH,
            g_render_content_y + y,
            cell,
            foreground_rgb,
            background_rgb
        );
    }
}

static void render_window(AstraDisplayWindow *window) {
    uint32_t title_color = 0u;
    uint32_t title_text = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;

    if (window == NULL || !window->visible) {
        return;
    }
    title_color = window->id == g_display.active_id ? 0x2e5f9du : 0x29374du;
    title_text = 0xf5f7fbu;
    astra_framebuffer_fill_rgb(
        window->x + 6u, window->y + 6u, window->width, window->height, 0x080d17u
    );
    astra_framebuffer_fill_rgb(
        window->x, window->y, window->width, window->height, 0x64748bu
    );
    astra_framebuffer_fill_rgb(
        window->x + DISPLAY_BORDER,
        window->y + DISPLAY_BORDER,
        window->width - DISPLAY_BORDER * 2u,
        DISPLAY_TITLE_BAR_HEIGHT - DISPLAY_BORDER,
        title_color
    );
    astra_framebuffer_fill_rgb(
        window->x + DISPLAY_BORDER,
        window->y + DISPLAY_TITLE_BAR_HEIGHT,
        window->width - DISPLAY_BORDER * 2u,
        window->height - DISPLAY_TITLE_BAR_HEIGHT - DISPLAY_BORDER,
        0xf1f5f9u
    );
    astra_framebuffer_draw_text(
        window->x + 12u, window->y + 7u, window->title,
        title_text, title_color
    );
    astra_framebuffer_fill_rgb(window->x + window->width - 54u,
        window->y + 10u, 8u, 8u, 0x35c58au);
    astra_framebuffer_fill_rgb(window->x + window->width - 38u,
        window->y + 10u, 8u, 8u, 0xf3bd4fu);
    astra_framebuffer_fill_rgb(window->x + window->width - 22u,
        window->y + 10u, 8u, 8u, 0xf06a71u);

    width = content_width(window);
    height = content_height(window);
    g_render_window = window;
    g_render_content_x = window->x + DISPLAY_CONTENT_INSET;
    g_render_content_y = window->y + DISPLAY_TITLE_BAR_HEIGHT;
    g_render_content_width = width;
    g_render_content_height = height;
    window->render(width, height, window->context);
    g_render_window = NULL;
}

void astra_display_server_present(void) {
    if (!g_display.active) {
        return;
    }

    astra_framebuffer_fill_rgb(0u, 0u, g_display.width, g_display.height, 0x101a2bu);
    astra_framebuffer_fill_rgb(0u, 0u, g_display.width, 30u, 0x17243au);
    astra_framebuffer_fill_rgb(0u, 30u, g_display.width, 2u, 0x426dacu);
    astra_framebuffer_fill_rgb(0u, g_display.height - 48u,
        g_display.width, 48u, 0x17243au);
    astra_framebuffer_fill_rgb(18u, 10u, 8u, 8u, 0x61b6ffu);
    astra_framebuffer_draw_text(34u, 7u, "ASTRAOS DESKTOP", 0xf1f5f9u, 0x17243au);
    astra_framebuffer_draw_text(18u, g_display.height - 38u,
        "1 Welcome   2 System   T Terminal   Q Return to shell",
        0xd8e2f0u, 0x17243au);

    for (uint32_t i = 0u; i < g_display.window_count; i++) {
        render_window(&g_display.windows[i]);
    }
}

void astra_display_server_shutdown(void) {
    g_display.active = false;
    g_display.window_count = 0u;
    g_display.active_id = 0u;
    g_render_window = NULL;
    astra_framebuffer_set_console_enabled(true);
    astra_framebuffer_clear();
}
