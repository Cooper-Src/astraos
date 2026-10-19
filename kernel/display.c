#include "display.h"

#include "framebuffer.h"
#include "input.h"
#include "uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    DISPLAY_SURFACE_CAP = 4u,
    DISPLAY_TITLEBAR_HEIGHT = 30u,
    DISPLAY_PANEL_HEIGHT = 36u,
    DISPLAY_DOCK_HEIGHT = 62u,
    DISPLAY_MIN_WIDTH = 480u,
    DISPLAY_MIN_HEIGHT = 360u,
    DISPLAY_MIN_WINDOW_WIDTH = 260u,
    DISPLAY_MIN_WINDOW_HEIGHT = 160u,
    DISPLAY_MAX_WINDOW_WIDTH = 560u,
    DISPLAY_MAX_WINDOW_HEIGHT = 380u,
    DISPLAY_TERMINAL_LINE_CAP = 64u,
    DISPLAY_TERMINAL_HISTORY = 6u,
    DISPLAY_TERMINAL_TEXT_CAP = 72u,
    DISPLAY_TEXT_CELL_WIDTH = 8u
};

#define COLOR_DESKTOP       UINT32_C(0x0b1424)
#define COLOR_DESKTOP_LEFT  UINT32_C(0x12243a)
#define COLOR_DESKTOP_ACCENT UINT32_C(0x18304b)
#define COLOR_PANEL         UINT32_C(0x0a101d)
#define COLOR_PANEL_EDGE    UINT32_C(0x253851)
#define COLOR_DOCK          UINT32_C(0x0c1421)
#define COLOR_WINDOW        UINT32_C(0x182437)
#define COLOR_WINDOW_EDGE   UINT32_C(0x30435c)
#define COLOR_TITLE_ACTIVE  UINT32_C(0x21445a)
#define COLOR_TITLE_IDLE    UINT32_C(0x253247)
#define COLOR_TEXT          UINT32_C(0xe8f0fa)
#define COLOR_MUTED         UINT32_C(0xa7b7cb)
#define COLOR_ACCENT        UINT32_C(0x57dfdf)
#define COLOR_BLUE          UINT32_C(0x79aafc)
#define COLOR_GREEN         UINT32_C(0x83d7ad)
#define COLOR_TERMINAL      UINT32_C(0x080f19)
#define COLOR_TERMINAL_TEXT UINT32_C(0xb9f6d2)
#define COLOR_WARNING       UINT32_C(0xf0c674)

typedef enum AstraDisplayApp {
    DISPLAY_APP_WELCOME = 0,
    DISPLAY_APP_TERMINAL,
    DISPLAY_APP_FILES,
    DISPLAY_APP_ABOUT
} AstraDisplayApp;

typedef struct AstraDisplaySurface {
    AstraDisplayApp app;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t z_order;
    bool visible;
    bool focused;
} AstraDisplaySurface;

static AstraDisplaySurface g_surfaces[DISPLAY_SURFACE_CAP];
static uint32_t g_screen_width;
static uint32_t g_screen_height;
static uint32_t g_next_z_order;
static char g_terminal_line[DISPLAY_TERMINAL_LINE_CAP];
static char g_terminal_history[DISPLAY_TERMINAL_HISTORY][DISPLAY_TERMINAL_TEXT_CAP];

static size_t string_length(const char *text) {
    size_t length = 0u;

    if (text == NULL) {
        return 0u;
    }
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static bool string_equals(const char *left, const char *right) {
    size_t index = 0u;

    if (left == NULL || right == NULL) {
        return false;
    }
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return false;
        }
        index++;
    }
    return left[index] == right[index];
}

static void string_copy(char *destination, size_t capacity, const char *source) {
    size_t index = 0u;

    if (destination == NULL || capacity == 0u) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    while (index + 1u < capacity && source[index] != '\0') {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

static void string_append(char *destination, size_t capacity, const char *source) {
    size_t current_length = string_length(destination);
    size_t source_index = 0u;

    if (destination == NULL || source == NULL || capacity == 0u ||
        current_length >= capacity) {
        return;
    }
    while (current_length + 1u < capacity && source[source_index] != '\0') {
        destination[current_length] = source[source_index];
        current_length++;
        source_index++;
    }
    destination[current_length] = '\0';
}

static const char *app_title(AstraDisplayApp app) {
    switch (app) {
        case DISPLAY_APP_WELCOME:
            return "Welcome to AstraOS";
        case DISPLAY_APP_TERMINAL:
            return "Terminal";
        case DISPLAY_APP_FILES:
            return "File Browser";
        case DISPLAY_APP_ABOUT:
            return "About AstraOS";
        default:
            return "Application";
    }
}

static AstraDisplaySurface *find_surface(AstraDisplayApp app) {
    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        if (g_surfaces[index].app == app) {
            return &g_surfaces[index];
        }
    }
    return NULL;
}

static void focus_surface(AstraDisplaySurface *surface) {
    if (surface == NULL || !surface->visible) {
        return;
    }
    if (g_next_z_order == UINT32_MAX) {
        uint32_t next_order = 1u;
        for (size_t pass = 0u; pass < DISPLAY_SURFACE_CAP; pass++) {
            AstraDisplaySurface *lowest = NULL;
            for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
                AstraDisplaySurface *candidate = &g_surfaces[index];
                if (!candidate->visible || candidate->z_order >= next_order) {
                    continue;
                }
                if (lowest == NULL || candidate->z_order < lowest->z_order) {
                    lowest = candidate;
                }
            }
            if (lowest == NULL) {
                break;
            }
            lowest->z_order = next_order;
            next_order++;
        }
        g_next_z_order = next_order;
    }
    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        g_surfaces[index].focused = false;
    }
    surface->focused = true;
    surface->z_order = g_next_z_order;
    g_next_z_order++;
}

static void place_surface(AstraDisplaySurface *surface) {
    uint32_t width = DISPLAY_MAX_WINDOW_WIDTH;
    uint32_t height = DISPLAY_MAX_WINDOW_HEIGHT;
    uint32_t index = 0u;

    if (surface == NULL || g_screen_width <= 40u || g_screen_height <= 90u) {
        return;
    }
    if (width > g_screen_width - 40u) {
        width = g_screen_width - 40u;
    }
    if (height > g_screen_height - 90u) {
        height = g_screen_height - 90u;
    }
    if (width < DISPLAY_MIN_WINDOW_WIDTH || height < DISPLAY_MIN_WINDOW_HEIGHT) {
        return;
    }
    surface->width = width;
    surface->height = height;
    for (size_t slot = 0u; slot < DISPLAY_SURFACE_CAP; slot++) {
        if (&g_surfaces[slot] == surface) {
            index = (uint32_t)slot;
            break;
        }
    }
    surface->x = ((g_screen_width - width) / 2u) + (index * 14u);
    surface->y = ((g_screen_height - height) / 2u) + (index * 12u);
    if (surface->x > g_screen_width - width - 8u) {
        surface->x = g_screen_width - width - 8u;
    }
    if (surface->y > g_screen_height - height - 8u) {
        surface->y = g_screen_height - height - 8u;
    }
}

static void open_surface(AstraDisplayApp app) {
    AstraDisplaySurface *surface = find_surface(app);

    if (surface == NULL) {
        return;
    }
    place_surface(surface);
    surface->visible = surface->width >= DISPLAY_MIN_WINDOW_WIDTH &&
        surface->height >= DISPLAY_MIN_WINDOW_HEIGHT;
    if (surface->visible) {
        focus_surface(surface);
    }
}

static void close_surface(AstraDisplaySurface *surface) {
    AstraDisplaySurface *topmost = NULL;

    if (surface == NULL) {
        return;
    }
    surface->visible = false;
    surface->focused = false;
    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        AstraDisplaySurface *candidate = &g_surfaces[index];
        if (candidate->visible &&
            (topmost == NULL || candidate->z_order > topmost->z_order)) {
            topmost = candidate;
        }
    }
    if (topmost != NULL) {
        focus_surface(topmost);
    }
}

static void draw_text(uint32_t x, uint32_t y, const char *text, uint32_t color) {
    astra_framebuffer_draw_text(x, y, text, string_length(text), color);
}

static void draw_desktop_icon(
    uint32_t x,
    uint32_t y,
    uint32_t color,
    const char *letter,
    const char *label
) {
    astra_framebuffer_fill_rect(x + 2u, y + 2u, 42u, 42u, COLOR_PANEL_EDGE);
    astra_framebuffer_fill_rect(x, y, 42u, 42u, color);
    draw_text(x + 17u, y + 13u, letter, COLOR_PANEL);
    draw_text(x - 3u, y + 48u, label, COLOR_TEXT);
}

static void render_desktop_background(void) {
    const uint32_t dock_y = g_screen_height - DISPLAY_DOCK_HEIGHT;

    astra_framebuffer_fill_rect(
        0u, 0u, g_screen_width, g_screen_height, COLOR_DESKTOP
    );
    astra_framebuffer_fill_rect(
        0u, DISPLAY_PANEL_HEIGHT, g_screen_width / 3u,
        dock_y > DISPLAY_PANEL_HEIGHT ? dock_y - DISPLAY_PANEL_HEIGHT : 0u,
        COLOR_DESKTOP_LEFT
    );
    astra_framebuffer_fill_rect(
        g_screen_width / 3u, DISPLAY_PANEL_HEIGHT,
        g_screen_width / 3u, dock_y > DISPLAY_PANEL_HEIGHT ?
            dock_y - DISPLAY_PANEL_HEIGHT : 0u,
        COLOR_DESKTOP_ACCENT
    );
    astra_framebuffer_fill_rect(
        g_screen_width - (g_screen_width / 5u), DISPLAY_PANEL_HEIGHT,
        g_screen_width / 5u, dock_y > DISPLAY_PANEL_HEIGHT ?
            dock_y - DISPLAY_PANEL_HEIGHT : 0u,
        COLOR_DESKTOP_LEFT
    );

    astra_framebuffer_fill_rect(
        0u, 0u, g_screen_width, DISPLAY_PANEL_HEIGHT, COLOR_PANEL
    );
    astra_framebuffer_fill_rect(
        0u, DISPLAY_PANEL_HEIGHT - 2u, g_screen_width, 2u, COLOR_PANEL_EDGE
    );
    astra_framebuffer_fill_rect(13u, 7u, 22u, 22u, COLOR_ACCENT);
    astra_framebuffer_fill_rect(18u, 12u, 12u, 12u, COLOR_PANEL);
    draw_text(43u, 11u, "ASTRAOS", COLOR_TEXT);
    draw_text(124u, 11u, "DESKTOP", COLOR_MUTED);

    if (g_screen_width > 260u) {
        draw_text(g_screen_width - 240u, 11u, "UEFI  |  AARCH64  |  ALPHA", COLOR_MUTED);
    }

    draw_desktop_icon(25u, 64u, COLOR_BLUE, "T", "Terminal");
    draw_desktop_icon(25u, 132u, COLOR_GREEN, "F", "Files");
    draw_desktop_icon(25u, 200u, COLOR_ACCENT, "A", "About");

    astra_framebuffer_fill_rect(
        0u, dock_y, g_screen_width, DISPLAY_DOCK_HEIGHT, COLOR_DOCK
    );
    astra_framebuffer_fill_rect(
        0u, dock_y, g_screen_width, 2u, COLOR_PANEL_EDGE
    );

    {
        const uint32_t tile_width = 42u;
        const uint32_t tile_gap = 14u;
        const uint32_t total_width = (tile_width * 3u) + (tile_gap * 2u);
        const uint32_t start_x = g_screen_width > total_width ?
            (g_screen_width - total_width) / 2u : 4u;
        const uint32_t tile_y = dock_y + 8u;

        astra_framebuffer_fill_rect(start_x, tile_y, tile_width, tile_width, COLOR_BLUE);
        draw_text(start_x + 17u, tile_y + 13u, "T", COLOR_PANEL);
        astra_framebuffer_fill_rect(
            start_x + tile_width + tile_gap, tile_y, tile_width, tile_width, COLOR_GREEN
        );
        draw_text(start_x + tile_width + tile_gap + 17u, tile_y + 13u, "F", COLOR_PANEL);
        astra_framebuffer_fill_rect(
            start_x + (tile_width + tile_gap) * 2u, tile_y,
            tile_width, tile_width, COLOR_ACCENT
        );
        draw_text(
            start_x + (tile_width + tile_gap) * 2u + 17u,
            tile_y + 13u, "A", COLOR_PANEL
        );
        draw_text(12u, dock_y + 23u, "T Terminal   F Files   A About", COLOR_MUTED);
        if (g_screen_width > 430u) {
            draw_text(g_screen_width - 218u, dock_y + 23u, "Q Return to shell", COLOR_MUTED);
        }
    }
}

static void render_window_content(const AstraDisplaySurface *surface) {
    const uint32_t left = surface->x + 18u;
    const uint32_t top = surface->y + DISPLAY_TITLEBAR_HEIGHT + 18u;
    const uint32_t width = surface->width;

    switch (surface->app) {
        case DISPLAY_APP_WELCOME:
            draw_text(left, top, "Welcome to AstraOS", COLOR_ACCENT);
            draw_text(left, top + 34u, "A small AArch64 operating system project.", COLOR_TEXT);
            draw_text(left, top + 58u, "The software display server is running.", COLOR_TEXT);
            draw_text(left, top + 94u, "Open an app with a keyboard shortcut:", COLOR_MUTED);
            draw_text(left + 10u, top + 124u, "T  Open Terminal", COLOR_TERMINAL_TEXT);
            draw_text(left + 10u, top + 148u, "F  Open File Browser", COLOR_GREEN);
            draw_text(left + 10u, top + 172u, "A  Open About", COLOR_BLUE);
            draw_text(left, surface->y + surface->height - 42u, "Q  Return to the command shell", COLOR_WARNING);
            break;
        case DISPLAY_APP_TERMINAL: {
            uint32_t row_y = top + 8u;
            uint32_t rows_available = surface->height > 132u ?
                (surface->height - 132u) / 22u : 0u;

            astra_framebuffer_fill_rect(
                surface->x + 12u, surface->y + DISPLAY_TITLEBAR_HEIGHT + 10u,
                width - 24u, surface->height - DISPLAY_TITLEBAR_HEIGHT - 22u,
                COLOR_TERMINAL
            );
            draw_text(left, row_y, "AstraOS in-kernel terminal", COLOR_ACCENT);
            row_y += 28u;
            if (rows_available > DISPLAY_TERMINAL_HISTORY) {
                rows_available = DISPLAY_TERMINAL_HISTORY;
            }
            for (uint32_t row = 0u; row < rows_available; row++) {
                const uint32_t history_index =
                    DISPLAY_TERMINAL_HISTORY - rows_available + row;
                if (g_terminal_history[history_index][0] != '\0') {
                    draw_text(left, row_y, g_terminal_history[history_index], COLOR_TERMINAL_TEXT);
                }
                row_y += 22u;
            }
            draw_text(left, surface->y + surface->height - 52u, "$", COLOR_ACCENT);
            draw_text(
                left + 3u * DISPLAY_TEXT_CELL_WIDTH,
                surface->y + surface->height - 52u,
                g_terminal_line, COLOR_TERMINAL_TEXT
            );
            draw_text(
                left, surface->y + surface->height - 28u,
                "help  uname  about  clear  exit", COLOR_MUTED
            );
            break;
        }
        case DISPLAY_APP_FILES:
            draw_text(left, top, "RAM filesystem", COLOR_ACCENT);
            draw_text(left, top + 34u, "/", COLOR_TEXT);
            draw_text(left + 18u, top + 58u, "etc/          configuration examples", COLOR_TEXT);
            draw_text(left + 18u, top + 82u, "home/         user files", COLOR_TEXT);
            draw_text(left + 18u, top + 106u, "tmp/          temporary files", COLOR_TEXT);
            draw_text(left, top + 150u, "The VFS is in memory and resets on reboot.", COLOR_WARNING);
            draw_text(left, top + 174u, "Persistent storage is not implemented yet.", COLOR_MUTED);
            break;
        case DISPLAY_APP_ABOUT:
            draw_text(left, top, "AstraOS", COLOR_ACCENT);
            draw_text(left, top + 34u, "Experimental AArch64 operating system", COLOR_TEXT);
            draw_text(left, top + 58u, "Software-rendered GOP framebuffer", COLOR_TEXT);
            draw_text(left, top + 82u, "In-kernel surfaces and z-ordered composition", COLOR_TEXT);
            draw_text(left, top + 106u, "Keyboard input via UEFI compatibility path", COLOR_TEXT);
            draw_text(left, top + 142u, "No userspace process isolation yet.", COLOR_WARNING);
            draw_text(left, top + 166u, "No native mouse or USB/HID input yet.", COLOR_MUTED);
            break;
        default:
            draw_text(left, top, "Unknown application", COLOR_WARNING);
            break;
    }
}

static void render_surface(const AstraDisplaySurface *surface) {
    uint32_t title_color = 0u;

    if (surface == NULL || !surface->visible) {
        return;
    }
    title_color = surface->focused ? COLOR_TITLE_ACTIVE : COLOR_TITLE_IDLE;
    astra_framebuffer_fill_rect(
        surface->x + 7u, surface->y + 7u,
        surface->width, surface->height, COLOR_PANEL
    );
    astra_framebuffer_fill_rect(
        surface->x, surface->y,
        surface->width, surface->height, COLOR_WINDOW_EDGE
    );
    astra_framebuffer_fill_rect(
        surface->x + 2u, surface->y + 2u,
        surface->width - 4u, surface->height - 4u, COLOR_WINDOW
    );
    astra_framebuffer_fill_rect(
        surface->x + 2u, surface->y + 2u,
        surface->width - 4u, DISPLAY_TITLEBAR_HEIGHT, title_color
    );
    draw_text(
        surface->x + 14u, surface->y + 10u,
        app_title(surface->app), COLOR_TEXT
    );
    astra_framebuffer_fill_rect(
        surface->x + surface->width - 28u,
        surface->y + 9u, 12u, 12u, surface->focused ? COLOR_ACCENT : COLOR_MUTED
    );
    astra_framebuffer_fill_rect(
        surface->x + surface->width - 24u,
        surface->y + 13u, 4u, 4u, title_color
    );
    render_window_content(surface);
}

static void render_desktop(void) {
    bool rendered[DISPLAY_SURFACE_CAP] = {false, false, false, false};

    render_desktop_background();
    for (size_t pass = 0u; pass < DISPLAY_SURFACE_CAP; pass++) {
        size_t selected = DISPLAY_SURFACE_CAP;
        uint32_t lowest_z = UINT32_MAX;

        for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
            const AstraDisplaySurface *candidate = &g_surfaces[index];
            if (!candidate->visible || rendered[index] ||
                candidate->z_order >= lowest_z) {
                continue;
            }
            lowest_z = candidate->z_order;
            selected = index;
        }
        if (selected == DISPLAY_SURFACE_CAP) {
            break;
        }
        render_surface(&g_surfaces[selected]);
        rendered[selected] = true;
    }
}

static void terminal_history_push(const char *text) {
    for (size_t row = 0u; row + 1u < DISPLAY_TERMINAL_HISTORY; row++) {
        string_copy(
            g_terminal_history[row], DISPLAY_TERMINAL_TEXT_CAP,
            g_terminal_history[row + 1u]
        );
    }
    string_copy(
        g_terminal_history[DISPLAY_TERMINAL_HISTORY - 1u],
        DISPLAY_TERMINAL_TEXT_CAP, text
    );
}

static void terminal_clear_history(void) {
    for (size_t row = 0u; row < DISPLAY_TERMINAL_HISTORY; row++) {
        g_terminal_history[row][0] = '\0';
    }
}

static void terminal_execute(void) {
    char echo[DISPLAY_TERMINAL_TEXT_CAP] = {0};

    string_copy(echo, sizeof echo, "$ ");
    string_append(echo, sizeof echo, g_terminal_line);
    terminal_history_push(echo);

    if (string_equals(g_terminal_line, "help")) {
        terminal_history_push("Commands: help uname about clear exit");
        astra_uart_puts("desktop: terminal command help\n");
    } else if (string_equals(g_terminal_line, "uname")) {
        terminal_history_push("AstraOS ARM64 / experimental");
        astra_uart_puts("desktop: terminal command uname\n");
    } else if (string_equals(g_terminal_line, "about")) {
        terminal_history_push("AstraOS software desktop prototype");
    } else if (string_equals(g_terminal_line, "clear")) {
        terminal_clear_history();
    } else if (string_equals(g_terminal_line, "exit")) {
        AstraDisplaySurface *terminal = find_surface(DISPLAY_APP_TERMINAL);
        close_surface(terminal);
        astra_uart_puts("desktop: terminal window closed\n");
    } else if (g_terminal_line[0] != '\0') {
        char message[DISPLAY_TERMINAL_TEXT_CAP] = {0};
        string_copy(message, sizeof message, "command not found: ");
        string_append(message, sizeof message, g_terminal_line);
        terminal_history_push(message);
    }

    g_terminal_line[0] = '\0';
}

static void handle_terminal_key(uint8_t key) {
    const size_t length = string_length(g_terminal_line);

    if (key == (uint8_t)'\r' || key == (uint8_t)'\n') {
        terminal_execute();
        return;
    }
    if (key == 8u || key == 127u) {
        if (length > 0u) {
            g_terminal_line[length - 1u] = '\0';
        }
        return;
    }
    if (key >= 0x20u && key <= 0x7eu &&
        length + 1u < sizeof g_terminal_line) {
        g_terminal_line[length] = (char)key;
        g_terminal_line[length + 1u] = '\0';
    }
}

static AstraDisplaySurface *focused_surface(void) {
    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        if (g_surfaces[index].visible && g_surfaces[index].focused) {
            return &g_surfaces[index];
        }
    }
    return NULL;
}

static void stop_display_server(void) {
    astra_framebuffer_set_console_enabled(true);
    astra_framebuffer_clear();
    astra_uart_puts("desktop: returned to shell\n");
}

bool astra_display_server_run(void) {
    uint8_t key = 0u;

    if (!astra_framebuffer_is_active() ||
        !astra_framebuffer_get_dimensions(&g_screen_width, &g_screen_height) ||
        g_screen_width < DISPLAY_MIN_WIDTH ||
        g_screen_height < DISPLAY_MIN_HEIGHT) {
        astra_uart_puts("Display server unavailable: GOP framebuffer is too small or inactive.\n");
        return false;
    }

    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        g_surfaces[index].app = (AstraDisplayApp)index;
        g_surfaces[index].x = 0u;
        g_surfaces[index].y = 0u;
        g_surfaces[index].width = 0u;
        g_surfaces[index].height = 0u;
        g_surfaces[index].z_order = 0u;
        g_surfaces[index].visible = false;
        g_surfaces[index].focused = false;
    }
    g_next_z_order = 1u;
    g_terminal_line[0] = '\0';
    terminal_clear_history();
    astra_framebuffer_set_console_enabled(false);
    open_surface(DISPLAY_APP_WELCOME);
    render_desktop();
    astra_uart_puts("ASTRAOS_DISPLAY_SERVER_READY\n");
    astra_uart_puts(
        "Display server: software compositor active; T terminal, F files, A about, Q shell.\n"
    );

    for (;;) {
        AstraDisplaySurface *focus = focused_surface();

        if (!astra_input_read_byte(&key)) {
            astra_uart_puts("Display server input failed; returning to the shell.\n");
            stop_display_server();
            return false;
        }

        if (focus != NULL && focus->app == DISPLAY_APP_TERMINAL) {
            handle_terminal_key(key);
            render_desktop();
            continue;
        }

        switch (key) {
            case 't':
            case 'T':
            case '1':
                open_surface(DISPLAY_APP_TERMINAL);
                astra_uart_puts("desktop: terminal window opened\n");
                break;
            case 'f':
            case 'F':
            case '2':
                open_surface(DISPLAY_APP_FILES);
                astra_uart_puts("desktop: file browser opened\n");
                break;
            case 'a':
            case 'A':
            case '3':
                open_surface(DISPLAY_APP_ABOUT);
                astra_uart_puts("desktop: about window focused\n");
                break;
            case 'c':
            case 'C':
                close_surface(focus);
                break;
            case 'q':
            case 'Q':
                stop_display_server();
                return true;
            default:
                break;
        }
        render_desktop();
    }
}
