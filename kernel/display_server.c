#include "display_server.h"

#include "framebuffer.h"

#include <stddef.h>
#include <stdint.h>

enum {
    DISPLAY_CELL_WIDTH = 8u,
    DISPLAY_CELL_HEIGHT = 16u,
    DISPLAY_MIN_WIDTH = 320u,
    DISPLAY_MIN_HEIGHT = 200u,
    DISPLAY_WINDOW_MIN_WIDTH = 160u,
    DISPLAY_WINDOW_MIN_HEIGHT = 96u,
    DISPLAY_WINDOW_TITLE_HEIGHT = 24u,
    DISPLAY_WINDOW_CONTENT_TOP = 28u,
    DISPLAY_WINDOW_CONTENT_INSET = 4u,
    DISPLAY_PANEL_HEIGHT = 32u,
    DISPLAY_DOCK_HEIGHT = 40u,
    DISPLAY_EDGE_GAP = 12u,
    DISPLAY_CASCADE_STEP = 24u
};

#define DISPLAY_COLOR_BACKGROUND UINT32_C(0x101827)
#define DISPLAY_COLOR_BACKGROUND_ALT UINT32_C(0x142139)
#define DISPLAY_COLOR_ACCENT UINT32_C(0x2f80ed)
#define DISPLAY_COLOR_ACCENT_DARK UINT32_C(0x1d4f91)
#define DISPLAY_COLOR_PANEL UINT32_C(0x0b111b)
#define DISPLAY_COLOR_PANEL_EDGE UINT32_C(0x26364b)
#define DISPLAY_COLOR_WINDOW UINT32_C(0x101722)
#define DISPLAY_COLOR_WINDOW_EDGE UINT32_C(0x42536b)
#define DISPLAY_COLOR_TITLE_ACTIVE UINT32_C(0x235fa8)
#define DISPLAY_COLOR_TITLE_INACTIVE UINT32_C(0x273448)
#define DISPLAY_COLOR_TEXT UINT32_C(0xf1f5f9)
#define DISPLAY_COLOR_MUTED UINT32_C(0x9fb0c5)
#define DISPLAY_COLOR_TERMINAL_BG UINT32_C(0x0a111b)
#define DISPLAY_COLOR_TERMINAL_FG UINT32_C(0x91e6b3)

static AstraDisplayWindow g_windows[ASTRA_DISPLAY_MAX_WINDOWS];
static size_t g_window_count;
static uint32_t g_focused_id;
static uint32_t g_next_id;
static uint32_t g_screen_width;
static uint32_t g_screen_height;
static bool g_active;

static size_t text_length(const char *text) {
    size_t length = 0u;
    if (text == NULL) {
        return 0u;
    }
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static size_t bounded_text_length(const char *text, size_t maximum) {
    size_t length = 0u;
    if (text == NULL) {
        return 0u;
    }
    while (length < maximum && text[length] != '\0') {
        length++;
    }
    return length;
}

static void clear_window(AstraDisplayWindow *window) {
    if (window == NULL) {
        return;
    }
    window->id = 0u;
    window->x = 0u;
    window->y = 0u;
    window->width = 0u;
    window->height = 0u;
    window->content_width = 0u;
    window->content_height = 0u;
    window->visible = false;
    window->focused = false;
    window->title[0] = '\0';
    window->paint = NULL;
    window->context = NULL;
}

static AstraDisplayWindow *find_window(uint32_t window_id) {
    if (window_id == 0u) {
        return NULL;
    }
    for (size_t i = 0u; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            return &g_windows[i];
        }
    }
    return NULL;
}

static void focus_window(uint32_t window_id) {
    AstraDisplayWindow *target = find_window(window_id);
    if (target == NULL || !target->visible) {
        return;
    }
    for (size_t i = 0u; i < g_window_count; i++) {
        g_windows[i].focused = g_windows[i].id == window_id;
    }
    g_focused_id = window_id;
}

static void draw_text_clipped(
    uint32_t x,
    uint32_t y,
    const char *text,
    size_t length,
    uint32_t foreground,
    uint32_t background
) {
    size_t max_chars = 0u;
    if (!g_active || text == NULL || x >= g_screen_width ||
        y >= g_screen_height || g_screen_height - y < DISPLAY_CELL_HEIGHT) {
        return;
    }
    max_chars = (size_t)((g_screen_width - x) / DISPLAY_CELL_WIDTH);
    if (length > max_chars) {
        length = max_chars;
    }
    astra_framebuffer_draw_text(x, y, text, length, foreground, background);
}

bool astra_display_server_initialize(void) {
    for (size_t i = 0u; i < ASTRA_DISPLAY_MAX_WINDOWS; i++) {
        clear_window(&g_windows[i]);
    }
    g_window_count = 0u;
    g_focused_id = 0u;
    g_next_id = 1u;
    g_screen_width = 0u;
    g_screen_height = 0u;
    g_active = false;

    if (!astra_framebuffer_is_active()) {
        return false;
    }
    g_screen_width = astra_framebuffer_width();
    g_screen_height = astra_framebuffer_height();
    if (g_screen_width < DISPLAY_MIN_WIDTH ||
        g_screen_height < DISPLAY_MIN_HEIGHT) {
        g_screen_width = 0u;
        g_screen_height = 0u;
        return false;
    }

    g_active = true;
    astra_framebuffer_set_console_enabled(false);
    astra_display_server_render();
    return true;
}

void astra_display_server_shutdown(void) {
    for (size_t i = 0u; i < ASTRA_DISPLAY_MAX_WINDOWS; i++) {
        clear_window(&g_windows[i]);
    }
    g_window_count = 0u;
    g_focused_id = 0u;
    g_active = false;
    g_screen_width = 0u;
    g_screen_height = 0u;
    astra_framebuffer_set_console_enabled(true);
    astra_framebuffer_clear();
}

bool astra_display_server_is_active(void) {
    return g_active;
}

uint32_t astra_display_server_width(void) {
    return g_active ? g_screen_width : 0u;
}

uint32_t astra_display_server_height(void) {
    return g_active ? g_screen_height : 0u;
}

size_t astra_display_server_window_count(void) {
    return g_window_count;
}

const AstraDisplayWindow *astra_display_server_focused_window(void) {
    return find_window(g_focused_id);
}

bool astra_display_server_open_window(
    const char *title,
    uint32_t width,
    uint32_t height,
    AstraDisplayWindowPaint paint,
    void *context,
    uint32_t *window_id_out
) {
    size_t title_len = 0u;
    uint32_t max_width = 0u;
    uint32_t max_height = 0u;
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t window_id = 0u;

    if (window_id_out != NULL) {
        *window_id_out = 0u;
    }
    if (!g_active || title == NULL || title[0] == '\0' ||
        width < DISPLAY_WINDOW_MIN_WIDTH || height < DISPLAY_WINDOW_MIN_HEIGHT) {
        return false;
    }

    title_len = text_length(title);
    for (size_t i = 0u; i < g_window_count; i++) {
        if (bounded_text_length(g_windows[i].title, ASTRA_DISPLAY_TITLE_CAP) ==
                title_len &&
            title_len < (size_t)ASTRA_DISPLAY_TITLE_CAP) {
            bool equal = true;
            for (size_t j = 0u; j < title_len; j++) {
                if (g_windows[i].title[j] != title[j]) {
                    equal = false;
                    break;
                }
            }
            if (equal) {
                focus_window(g_windows[i].id);
                if (window_id_out != NULL) {
                    *window_id_out = g_windows[i].id;
                }
                return true;
            }
        }
    }
    if (g_window_count >= (size_t)ASTRA_DISPLAY_MAX_WINDOWS) {
        return false;
    }

    max_width = g_screen_width - (DISPLAY_EDGE_GAP * 2u);
    max_height = g_screen_height - DISPLAY_DOCK_HEIGHT - DISPLAY_PANEL_HEIGHT;
    if (width > max_width) {
        width = max_width;
    }
    if (height > max_height) {
        height = max_height;
    }
    if (width < DISPLAY_WINDOW_MIN_WIDTH || height < DISPLAY_WINDOW_MIN_HEIGHT) {
        return false;
    }

    x = (g_screen_width - width) / 2u;
    y = (g_screen_height - height) / 2u;
    if (g_window_count <=
        (size_t)((g_screen_width - width - DISPLAY_EDGE_GAP) / DISPLAY_CASCADE_STEP)) {
        x += (uint32_t)g_window_count * DISPLAY_CASCADE_STEP;
    }
    if (x > g_screen_width - width - DISPLAY_EDGE_GAP) {
        x = g_screen_width - width - DISPLAY_EDGE_GAP;
    }
    if (g_window_count <=
        (size_t)((g_screen_height - height - DISPLAY_DOCK_HEIGHT) / DISPLAY_CASCADE_STEP)) {
        y += (uint32_t)g_window_count * DISPLAY_CASCADE_STEP;
    }
    if (y > g_screen_height - height - DISPLAY_DOCK_HEIGHT) {
        y = g_screen_height - height - DISPLAY_DOCK_HEIGHT;
    }

    window_id = g_next_id;
    g_next_id++;
    if (g_next_id == 0u) {
        g_next_id = 1u;
    }
    if (window_id == 0u) {
        window_id = g_next_id;
        g_next_id++;
        if (g_next_id == 0u) {
            g_next_id = 1u;
        }
    }

    clear_window(&g_windows[g_window_count]);
    g_windows[g_window_count].id = window_id;
    g_windows[g_window_count].x = x;
    g_windows[g_window_count].y = y;
    g_windows[g_window_count].width = width;
    g_windows[g_window_count].height = height;
    g_windows[g_window_count].content_width = width - (DISPLAY_WINDOW_CONTENT_INSET * 2u);
    g_windows[g_window_count].content_height =
        height - DISPLAY_WINDOW_CONTENT_TOP - DISPLAY_WINDOW_CONTENT_INSET;
    g_windows[g_window_count].visible = true;
    g_windows[g_window_count].focused = false;
    for (size_t i = 0u; i < title_len && i + 1u < (size_t)ASTRA_DISPLAY_TITLE_CAP; i++) {
        g_windows[g_window_count].title[i] = title[i];
    }
    g_windows[g_window_count].title[
        title_len < (size_t)ASTRA_DISPLAY_TITLE_CAP
            ? title_len : (size_t)ASTRA_DISPLAY_TITLE_CAP - 1u] = '\0';
    g_windows[g_window_count].paint = paint;
    g_windows[g_window_count].context = context;
    g_window_count++;
    focus_window(window_id);
    if (window_id_out != NULL) {
        *window_id_out = window_id;
    }
    return true;
}

bool astra_display_server_close_window(uint32_t window_id) {
    size_t index = g_window_count;
    if (!g_active || window_id == 0u) {
        return false;
    }
    for (size_t i = 0u; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            index = i;
            break;
        }
    }
    if (index >= g_window_count) {
        return false;
    }
    for (size_t i = index; i + 1u < g_window_count; i++) {
        g_windows[i] = g_windows[i + 1u];
    }
    g_window_count--;
    clear_window(&g_windows[g_window_count]);
    if (g_window_count == 0u) {
        g_focused_id = 0u;
    } else {
        focus_window(g_windows[g_window_count - 1u].id);
    }
    return true;
}

bool astra_display_server_focus_next(void) {
    size_t current = g_window_count;
    if (!g_active || g_window_count < 2u) {
        return false;
    }
    for (size_t i = 0u; i < g_window_count; i++) {
        if (g_windows[i].id == g_focused_id) {
            current = i;
            break;
        }
    }
    if (current >= g_window_count) {
        focus_window(g_windows[0].id);
    } else {
        focus_window(g_windows[(current + 1u) % g_window_count].id);
    }
    return true;
}

static void draw_desktop(void) {
    uint32_t dock_y = 0u;
    uint32_t label_x = 14u;
    const char *dock_label = "1 Terminal  2 System  3 Files  TAB Focus  ESC Shell";
    size_t dock_length = text_length(dock_label);

    astra_framebuffer_fill_rect(0u, 0u, g_screen_width, g_screen_height,
        DISPLAY_COLOR_BACKGROUND);
    for (uint32_t y = DISPLAY_PANEL_HEIGHT; y + 52u < g_screen_height;
        y += 52u) {
        uint32_t stripe = ((y / 52u) % 2u) == 0u
            ? DISPLAY_COLOR_BACKGROUND_ALT : DISPLAY_COLOR_BACKGROUND;
        astra_framebuffer_fill_rect(0u, y, g_screen_width, 24u, stripe);
    }

    astra_framebuffer_fill_rect(0u, 0u, g_screen_width, DISPLAY_PANEL_HEIGHT,
        DISPLAY_COLOR_PANEL);
    astra_framebuffer_fill_rect(0u, DISPLAY_PANEL_HEIGHT - 2u,
        g_screen_width, 2u, DISPLAY_COLOR_ACCENT);
    draw_text_clipped(14u, 8u, "ASTRAOS", 7u, DISPLAY_COLOR_TEXT,
        DISPLAY_COLOR_PANEL);
    if (g_screen_width >= 400u) {
        draw_text_clipped(g_screen_width - 20u * DISPLAY_CELL_WIDTH - 14u, 8u,
            "AARCH64  |  DESKTOP", 20u, DISPLAY_COLOR_MUTED, DISPLAY_COLOR_PANEL);
    }

    /* Three clearly labeled shortcuts are keyboard-launchable by number. */
    for (uint32_t i = 0u; i < 3u; i++) {
        uint32_t icon_y = 62u + (i * 76u);
        const uint32_t icon_colors[3] = {
            DISPLAY_COLOR_ACCENT, UINT32_C(0x1e9a84), UINT32_C(0xb77931)
        };
        const char *labels[3] = {"Terminal", "System", "Files"};
        if (icon_y + 52u >= g_screen_height - DISPLAY_DOCK_HEIGHT) {
            break;
        }
        astra_framebuffer_fill_rect(22u, icon_y, 36u, 36u, icon_colors[i]);
        astra_framebuffer_fill_rect(26u, icon_y + 4u, 28u, 28u,
            DISPLAY_COLOR_PANEL);
        draw_text_clipped(34u, icon_y + 10u, i == 0u ? ">" : (i == 1u ? "i" : "/"),
            1u, DISPLAY_COLOR_TEXT, DISPLAY_COLOR_PANEL);
        draw_text_clipped(14u, icon_y + 40u, labels[i], text_length(labels[i]),
            DISPLAY_COLOR_TEXT, DISPLAY_COLOR_BACKGROUND);
    }

    dock_y = g_screen_height - DISPLAY_DOCK_HEIGHT;
    astra_framebuffer_fill_rect(0u, dock_y, g_screen_width, DISPLAY_DOCK_HEIGHT,
        DISPLAY_COLOR_PANEL);
    astra_framebuffer_fill_rect(0u, dock_y, g_screen_width, 1u,
        DISPLAY_COLOR_PANEL_EDGE);
    if ((g_screen_width - label_x) / DISPLAY_CELL_WIDTH < dock_length) {
        dock_length = (size_t)((g_screen_width - label_x) / DISPLAY_CELL_WIDTH);
    }
    draw_text_clipped(label_x, dock_y + 12u, dock_label, dock_length,
        DISPLAY_COLOR_TEXT, DISPLAY_COLOR_PANEL);
}

static void draw_window(const AstraDisplayWindow *window) {
    uint32_t frame_color = DISPLAY_COLOR_WINDOW_EDGE;
    uint32_t title_color = DISPLAY_COLOR_TITLE_INACTIVE;
    size_t title_len = 0u;

    if (window == NULL || !window->visible || window->width < 8u ||
        window->height < 32u) {
        return;
    }
    if (window->focused) {
        title_color = DISPLAY_COLOR_TITLE_ACTIVE;
        frame_color = DISPLAY_COLOR_ACCENT;
    }

    astra_framebuffer_fill_rect(window->x + 4u, window->y + 4u,
        window->width, window->height, UINT32_C(0x070b12));
    astra_framebuffer_fill_rect(window->x, window->y, window->width,
        window->height, frame_color);
    astra_framebuffer_fill_rect(window->x + 2u, window->y + 2u,
        window->width - 4u, DISPLAY_WINDOW_TITLE_HEIGHT, title_color);
    astra_framebuffer_fill_rect(window->x + DISPLAY_WINDOW_CONTENT_INSET,
        window->y + DISPLAY_WINDOW_CONTENT_TOP,
        window->width - (DISPLAY_WINDOW_CONTENT_INSET * 2u),
        window->height - DISPLAY_WINDOW_CONTENT_TOP - 2u, DISPLAY_COLOR_WINDOW);

    title_len = bounded_text_length(window->title, (size_t)ASTRA_DISPLAY_TITLE_CAP - 1u);
    draw_text_clipped(window->x + 10u, window->y + 6u, window->title,
        title_len, DISPLAY_COLOR_TEXT, title_color);
    if (window->paint != NULL) {
        window->paint(window, window->context);
    }
}

void astra_display_server_render(void) {
    const AstraDisplayWindow *focused = NULL;
    if (!g_active) {
        return;
    }
    draw_desktop();
    for (size_t i = 0u; i < g_window_count; i++) {
        if (g_windows[i].id == g_focused_id) {
            focused = &g_windows[i];
            continue;
        }
        draw_window(&g_windows[i]);
    }
    draw_window(focused);
}

void astra_display_window_fill(
    const AstraDisplayWindow *window,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t rgb
) {
    uint32_t screen_x = 0u;
    uint32_t screen_y = 0u;

    if (!g_active || window == NULL || !window->visible ||
        x >= window->content_width || y >= window->content_height) {
        return;
    }
    if (width > window->content_width - x) {
        width = window->content_width - x;
    }
    if (height > window->content_height - y) {
        height = window->content_height - y;
    }
    screen_x = window->x + DISPLAY_WINDOW_CONTENT_INSET + x;
    screen_y = window->y + DISPLAY_WINDOW_CONTENT_TOP + y;
    astra_framebuffer_fill_rect(screen_x, screen_y, width, height, rgb);
}

void astra_display_window_text(
    const AstraDisplayWindow *window,
    uint32_t x,
    uint32_t y,
    const char *text,
    size_t length,
    uint32_t foreground_rgb,
    uint32_t background_rgb
) {
    uint32_t cell_x = x;
    uint32_t line_y = y;

    if (!g_active || window == NULL || !window->visible || text == NULL ||
        x >= window->content_width || y >= window->content_height) {
        return;
    }
    for (size_t i = 0u; i < length; i++) {
        const uint8_t character = (uint8_t)text[i];
        if (character == (uint8_t)'\\r') {
            continue;
        }
        if (character == (uint8_t)'\\n') {
            if (line_y >= window->content_height ||
                window->content_height - line_y < DISPLAY_CELL_HEIGHT ||
                window->content_height - line_y - DISPLAY_CELL_HEIGHT <
                    DISPLAY_CELL_HEIGHT) {
                return;
            }
            line_y += DISPLAY_CELL_HEIGHT;
            cell_x = x;
            continue;
        }
        if (cell_x >= window->content_width ||
            window->content_width - cell_x < DISPLAY_CELL_WIDTH) {
            if (line_y >= window->content_height ||
                window->content_height - line_y < DISPLAY_CELL_HEIGHT ||
                window->content_height - line_y - DISPLAY_CELL_HEIGHT <
                    DISPLAY_CELL_HEIGHT) {
                return;
            }
            line_y += DISPLAY_CELL_HEIGHT;
            cell_x = x;
        }
        if (line_y >= window->content_height ||
            window->content_height - line_y < DISPLAY_CELL_HEIGHT) {
            return;
        }
        astra_framebuffer_draw_text(
            window->x + DISPLAY_WINDOW_CONTENT_INSET + cell_x,
            window->y + DISPLAY_WINDOW_CONTENT_TOP + line_y,
            &text[i], 1u, foreground_rgb, background_rgb
        );
        cell_x += DISPLAY_CELL_WIDTH;
    }
}
