#include "display.h"

#include "framebuffer.h"
#include "input.h"
#include "system.h"
#include "uart.h"
#include "vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    DISPLAY_SURFACE_CAP = 5u,
    DISPLAY_TITLEBAR_HEIGHT = 30u,
    DISPLAY_PANEL_HEIGHT = 42u,
    DISPLAY_DOCK_HEIGHT = 54u,
    DISPLAY_MIN_WIDTH = 480u,
    DISPLAY_MIN_HEIGHT = 360u,
    DISPLAY_MIN_WINDOW_WIDTH = 260u,
    DISPLAY_MIN_WINDOW_HEIGHT = 180u,
    DISPLAY_MAX_WINDOW_WIDTH = 720u,
    DISPLAY_MAX_WINDOW_HEIGHT = 500u,
    DISPLAY_TERMINAL_LINE_CAP = 52u,
    DISPLAY_TERMINAL_HISTORY = 10u,
    DISPLAY_TERMINAL_TEXT_CAP = 54u,
    DISPLAY_FILE_ROWS = 8u,
    DISPLAY_VIEWER_ROWS = 12u,
    DISPLAY_TEXT_CELL_WIDTH = 8u,
    DISPLAY_DOUBLE_CLICK_EVENT_LIMIT = 8u
};

#define COLOR_DESKTOP          UINT32_C(0x101b2c)
#define COLOR_DESKTOP_LEFT     UINT32_C(0x172940)
#define COLOR_DESKTOP_ACCENT   UINT32_C(0x203651)
#define COLOR_PANEL            UINT32_C(0x111b2b)
#define COLOR_PANEL_EDGE       UINT32_C(0x34465f)
#define COLOR_PANEL_HOVER      UINT32_C(0x23354d)
#define COLOR_DOCK              UINT32_C(0x111a29)
#define COLOR_WINDOW            UINT32_C(0x1a2638)
#define COLOR_WINDOW_EDGE      UINT32_C(0x435570)
#define COLOR_TITLE_ACTIVE     UINT32_C(0x2c526a)
#define COLOR_TITLE_IDLE       UINT32_C(0x25344a)
#define COLOR_TEXT             UINT32_C(0xeaf1fa)
#define COLOR_MUTED            UINT32_C(0xaab9cc)
#define COLOR_ACCENT           UINT32_C(0x65e0dc)
#define COLOR_BLUE              UINT32_C(0x80adff)
#define COLOR_GREEN             UINT32_C(0x8cdbad)
#define COLOR_TERMINAL          UINT32_C(0x0a111b)
#define COLOR_TERMINAL_TEXT    UINT32_C(0xc3f5d5)
#define COLOR_WARNING          UINT32_C(0xf2c879)
#define COLOR_SELECTION        UINT32_C(0x2b5066)
#define COLOR_CLOSE            UINT32_C(0xe97979)
#define COLOR_MINIMIZE         UINT32_C(0xe8c16d)

typedef enum AstraDisplayApp {
    DISPLAY_APP_WELCOME = 0,
    DISPLAY_APP_TERMINAL,
    DISPLAY_APP_FILES,
    DISPLAY_APP_ABOUT,
    DISPLAY_APP_VIEWER
} AstraDisplayApp;

typedef struct AstraDisplaySurface {
    AstraDisplayApp app;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t restore_x;
    uint32_t restore_y;
    uint32_t restore_width;
    uint32_t restore_height;
    uint32_t z_order;
    bool visible;
    bool focused;
    bool minimized;
    bool maximized;
} AstraDisplaySurface;

static AstraDisplaySurface g_surfaces[DISPLAY_SURFACE_CAP];
static uint32_t g_screen_width;
static uint32_t g_screen_height;
static uint32_t g_next_z_order;
static char g_terminal_line[DISPLAY_TERMINAL_LINE_CAP];
static char g_terminal_history[DISPLAY_TERMINAL_HISTORY][DISPLAY_TERMINAL_TEXT_CAP];
static char g_files_path[ASTRA_VFS_PATH_CAP];
static size_t g_files_selection;
static size_t g_files_scroll;
static char g_viewer_path[ASTRA_VFS_PATH_CAP];
static char g_viewer_content[ASTRA_VFS_CONTENT_CAP + 1u];
static size_t g_viewer_content_length;
static size_t g_viewer_scroll;
static bool g_menu_open;
static uint32_t g_mouse_x;
static uint32_t g_mouse_y;
static bool g_mouse_left_down;
static bool g_logged_pointer_event = false;
static int g_drag_surface_index = -1;
static uint32_t g_pointer_debug_reports;
static uint32_t g_input_event_counter;
static uint32_t g_files_last_click_event;
static size_t g_files_last_click_index = SIZE_MAX;

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
    size_t current_length = 0u;
    size_t source_index = 0u;

    if (destination == NULL || source == NULL || capacity == 0u) {
        return;
    }
    current_length = string_length(destination);
    if (current_length >= capacity) {
        return;
    }
    while (current_length + 1u < capacity && source[source_index] != '\0') {
        destination[current_length] = source[source_index];
        current_length++;
        source_index++;
    }
    destination[current_length] = '\0';
}

static size_t skip_spaces(const char *text, size_t length, size_t start) {
    while (start < length && (text[start] == ' ' || text[start] == '\t')) {
        start++;
    }
    return start;
}

static const char *app_title(AstraDisplayApp app) {
    switch (app) {
        case DISPLAY_APP_WELCOME:
            return "Welcome";
        case DISPLAY_APP_TERMINAL:
            return "Terminal";
        case DISPLAY_APP_FILES:
            return "File Manager";
        case DISPLAY_APP_ABOUT:
            return "About AstraOS";
        case DISPLAY_APP_VIEWER:
            return "Text Viewer";
        default:
            return "Application";
    }
}

static const char *app_short_label(AstraDisplayApp app) {
    switch (app) {
        case DISPLAY_APP_WELCOME:
            return "Home";
        case DISPLAY_APP_TERMINAL:
            return "Terminal";
        case DISPLAY_APP_FILES:
            return "Files";
        case DISPLAY_APP_ABOUT:
            return "About";
        case DISPLAY_APP_VIEWER:
            return "Text";
        default:
            return "App";
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

static AstraDisplaySurface *focused_surface(void) {
    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        if (g_surfaces[index].visible && g_surfaces[index].focused) {
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
        bool assigned[DISPLAY_SURFACE_CAP];
        uint32_t next_order = 1u;

        for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
            assigned[index] = false;
        }

        for (size_t pass = 0u; pass < DISPLAY_SURFACE_CAP; pass++) {
            size_t selected = DISPLAY_SURFACE_CAP;
            uint32_t lowest_z = UINT32_MAX;

            for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
                const AstraDisplaySurface *candidate = &g_surfaces[index];
                if (!candidate->visible || assigned[index] ||
                    candidate->z_order >= lowest_z) {
                    continue;
                }
                selected = index;
                lowest_z = candidate->z_order;
            }
            if (selected == DISPLAY_SURFACE_CAP) {
                break;
            }
            assigned[selected] = true;
            g_surfaces[selected].z_order = next_order;
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
    size_t slot = 0u;

    if (surface == NULL || g_screen_width <= 40u ||
        g_screen_height <= DISPLAY_PANEL_HEIGHT + DISPLAY_DOCK_HEIGHT + 24u) {
        return;
    }
    if (width > g_screen_width - 40u) {
        width = g_screen_width - 40u;
    }
    if (height > g_screen_height - DISPLAY_PANEL_HEIGHT - DISPLAY_DOCK_HEIGHT - 24u) {
        height = g_screen_height - DISPLAY_PANEL_HEIGHT - DISPLAY_DOCK_HEIGHT - 24u;
    }
    if (width < DISPLAY_MIN_WINDOW_WIDTH || height < DISPLAY_MIN_WINDOW_HEIGHT) {
        return;
    }
    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        if (&g_surfaces[index] == surface) {
            slot = index;
            break;
        }
    }
    surface->width = width;
    surface->height = height;
    surface->x = (g_screen_width - width) / 2u + (uint32_t)slot * 12u;
    surface->y = DISPLAY_PANEL_HEIGHT + 8u +
        (g_screen_height - DISPLAY_PANEL_HEIGHT - DISPLAY_DOCK_HEIGHT - height - 16u) / 2u
        + (uint32_t)slot * 10u;
    if (surface->x > g_screen_width - width - 8u) {
        surface->x = g_screen_width - width - 8u;
    }
    if (surface->y > g_screen_height - DISPLAY_DOCK_HEIGHT - height - 8u) {
        surface->y = g_screen_height - DISPLAY_DOCK_HEIGHT - height - 8u;
    }
    surface->restore_x = surface->x;
    surface->restore_y = surface->y;
    surface->restore_width = surface->width;
    surface->restore_height = surface->height;
}

static void open_surface(AstraDisplayApp app) {
    AstraDisplaySurface *surface = find_surface(app);

    if (surface == NULL) {
        return;
    }
    if (surface->width == 0u || surface->height == 0u) {
        place_surface(surface);
    }
    if (surface->width < DISPLAY_MIN_WINDOW_WIDTH ||
        surface->height < DISPLAY_MIN_WINDOW_HEIGHT) {
        return;
    }
    surface->visible = true;
    surface->minimized = false;
    focus_surface(surface);
}

static void focus_next_surface(void) {
    AstraDisplaySurface *current = focused_surface();
    AstraDisplaySurface *next = NULL;
    uint32_t current_z = 0u;
    uint32_t lowest_z = UINT32_MAX;

    if (current == NULL) {
        for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
            AstraDisplaySurface *candidate = &g_surfaces[index];
            if (candidate->visible &&
                (next == NULL || candidate->z_order > next->z_order)) {
                next = candidate;
            }
        }
        focus_surface(next);
        return;
    }

    current_z = current->z_order;
    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        AstraDisplaySurface *candidate = &g_surfaces[index];
        if (candidate->visible && candidate != current &&
            candidate->z_order < lowest_z && candidate->z_order > current_z) {
            next = candidate;
            lowest_z = candidate->z_order;
        }
    }
    if (next == NULL) {
        for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
            AstraDisplaySurface *candidate = &g_surfaces[index];
            if (candidate->visible && candidate != current &&
                candidate->z_order < lowest_z) {
                next = candidate;
                lowest_z = candidate->z_order;
            }
        }
    }
    if (next != NULL) {
        focus_surface(next);
    }
    astra_uart_puts("desktop: focused next window\n");
}

static void focus_topmost_surface(void) {
    AstraDisplaySurface *topmost = NULL;

    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        AstraDisplaySurface *candidate = &g_surfaces[index];
        if (candidate->visible &&
            (topmost == NULL || candidate->z_order > topmost->z_order)) {
            topmost = candidate;
        }
    }
    focus_surface(topmost);
}

static void close_surface(AstraDisplaySurface *surface) {
    if (surface == NULL) {
        return;
    }
    surface->visible = false;
    surface->focused = false;
    surface->minimized = false;
    surface->maximized = false;
    focus_topmost_surface();
}

static void minimize_surface(AstraDisplaySurface *surface) {
    if (surface == NULL || !surface->visible) {
        return;
    }
    surface->visible = false;
    surface->focused = false;
    surface->minimized = true;
    focus_topmost_surface();
    astra_uart_puts("desktop: window minimized\n");
}

static void toggle_maximize_surface(AstraDisplaySurface *surface) {
    if (surface == NULL || !surface->visible) {
        return;
    }
    if (surface->maximized) {
        surface->x = surface->restore_x;
        surface->y = surface->restore_y;
        surface->width = surface->restore_width;
        surface->height = surface->restore_height;
        surface->maximized = false;
    } else {
        surface->restore_x = surface->x;
        surface->restore_y = surface->y;
        surface->restore_width = surface->width;
        surface->restore_height = surface->height;
        surface->x = 10u;
        surface->y = DISPLAY_PANEL_HEIGHT + 6u;
        surface->width = g_screen_width - 20u;
        surface->height = g_screen_height - DISPLAY_PANEL_HEIGHT -
            DISPLAY_DOCK_HEIGHT - 12u;
        surface->maximized = true;
    }
    focus_surface(surface);
    astra_uart_puts(surface->maximized ?
        "desktop: window maximized\n" : "desktop: window restored\n");
}

static void draw_text(uint32_t x, uint32_t y, const char *text, uint32_t color) {
    if (text != NULL) {
        astra_framebuffer_draw_text(x, y, text, string_length(text), color);
    }
}

static void draw_desktop_icon(
    uint32_t x,
    uint32_t y,
    uint32_t color,
    const char *letter,
    const char *label
) {
    astra_framebuffer_fill_rect(x + 3u, y + 3u, 42u, 42u, COLOR_PANEL);
    astra_framebuffer_fill_rect(x, y, 42u, 42u, COLOR_WINDOW_EDGE);
    astra_framebuffer_fill_rect(x + 2u, y + 2u, 38u, 38u, color);
    draw_text(x + 17u, y + 14u, letter, COLOR_PANEL);
    draw_text(x - 3u, y + 49u, label, COLOR_TEXT);
}

static void render_panel(void) {
    uint32_t right_edge = g_screen_width > 126u ? g_screen_width - 126u : 0u;
    uint32_t task_x = 152u;
    const uint32_t task_width = 96u;

    astra_framebuffer_fill_rect(0u, 0u, g_screen_width, DISPLAY_PANEL_HEIGHT, COLOR_PANEL);
    astra_framebuffer_fill_rect(
        0u, DISPLAY_PANEL_HEIGHT - 2u, g_screen_width, 2u, COLOR_PANEL_EDGE
    );

    astra_framebuffer_fill_rect(10u, 7u, 27u, 27u, COLOR_ACCENT);
    astra_framebuffer_fill_rect(15u, 12u, 17u, 17u, COLOR_PANEL);
    draw_text(20u, 17u, "A", COLOR_ACCENT);
    astra_framebuffer_fill_rect(43u, 5u, 100u, 32u, COLOR_PANEL_HOVER);
    draw_text(65u, 17u, "Start", COLOR_TEXT);

    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        const AstraDisplaySurface *surface = &g_surfaces[index];
        if (!surface->visible && !surface->minimized) {
            continue;
        }
        if (task_x + task_width > right_edge) {
            break;
        }
        astra_framebuffer_fill_rect(
            task_x, 5u, task_width - 3u, 32u,
            surface->focused ? COLOR_SELECTION : COLOR_PANEL
        );
        astra_framebuffer_fill_rect(
            task_x, 5u, 3u, 32u,
            surface->focused ? COLOR_ACCENT : COLOR_PANEL_EDGE
        );
        draw_text(task_x + 9u, 17u, app_short_label(surface->app), COLOR_TEXT);
        if (surface->minimized) {
            astra_framebuffer_fill_rect(
                task_x + task_width - 14u, 18u, 5u, 5u, COLOR_MINIMIZE
            );
        } else if (surface->focused) {
            astra_framebuffer_fill_rect(
                task_x + task_width - 14u, 18u, 5u, 5u, COLOR_ACCENT
            );
        }
        task_x += task_width;
    }

    if (g_screen_width > 320u) {
        draw_text(g_screen_width - 116u, 17u, "WS 1 | ARM64", COLOR_MUTED);
    }
}

static void render_desktop_background(void) {
    uint32_t dock_y = g_screen_height - DISPLAY_DOCK_HEIGHT;
    uint32_t middle_width = g_screen_width / 3u;

    astra_framebuffer_fill_rect(
        0u, 0u, g_screen_width, g_screen_height, COLOR_DESKTOP
    );
    astra_framebuffer_fill_rect(
        0u, DISPLAY_PANEL_HEIGHT, middle_width,
        dock_y - DISPLAY_PANEL_HEIGHT, COLOR_DESKTOP_LEFT
    );
    astra_framebuffer_fill_rect(
        middle_width, DISPLAY_PANEL_HEIGHT, middle_width,
        dock_y - DISPLAY_PANEL_HEIGHT, COLOR_DESKTOP_ACCENT
    );
    astra_framebuffer_fill_rect(
        g_screen_width - g_screen_width / 5u, DISPLAY_PANEL_HEIGHT,
        g_screen_width / 5u, dock_y - DISPLAY_PANEL_HEIGHT, COLOR_DESKTOP_LEFT
    );

    /* The subdued geometric wallpaper is deliberately drawn with bounded rectangles. */
    astra_framebuffer_fill_rect(
        g_screen_width / 2u, DISPLAY_PANEL_HEIGHT + 28u,
        g_screen_width / 5u, 3u, COLOR_WINDOW_EDGE
    );
    astra_framebuffer_fill_rect(
        g_screen_width / 2u + 18u, DISPLAY_PANEL_HEIGHT + 45u,
        g_screen_width / 7u, 3u, COLOR_PANEL_EDGE
    );
    render_panel();

    draw_desktop_icon(25u, 66u, COLOR_BLUE, "T", "Terminal");
    draw_desktop_icon(25u, 134u, COLOR_GREEN, "F", "Files");
    draw_desktop_icon(25u, 202u, COLOR_ACCENT, "A", "About");

    astra_framebuffer_fill_rect(0u, dock_y, g_screen_width, DISPLAY_DOCK_HEIGHT, COLOR_DOCK);
    astra_framebuffer_fill_rect(0u, dock_y, g_screen_width, 2u, COLOR_PANEL_EDGE);

    if (g_screen_width > 660u) {
        draw_text(14u, dock_y + 13u, "T Terminal   F Files   A About", COLOR_MUTED);
        draw_text(
            14u, dock_y + 31u, "M Menu  W Switch window  C Close  N Minimise  X Maximise",
            COLOR_MUTED
        );
        draw_text(g_screen_width - 204u, dock_y + 22u, "AstraOS Desktop", COLOR_TEXT);
    } else {
        draw_text(12u, dock_y + 13u, "T Terminal  F Files  A About", COLOR_MUTED);
        draw_text(12u, dock_y + 31u, "M Menu  C Close  N Min  X Max  Q Shell", COLOR_MUTED);
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

static void terminal_report_vfs(AstraVfsResult result) {
    char message[DISPLAY_TERMINAL_TEXT_CAP];

    string_copy(message, sizeof message, "error: ");
    string_append(message, sizeof message, astra_vfs_result_name(result));
    terminal_history_push(message);
}

static void terminal_command_pwd(AstraVfs *fs) {
    char path[ASTRA_VFS_PATH_CAP];
    size_t path_length = 0u;
    const AstraVfsResult result = astra_vfs_pwd(
        fs, path, sizeof path, &path_length
    );

    if (result != ASTRA_VFS_OK) {
        terminal_report_vfs(result);
        return;
    }
    (void)path_length;
    terminal_history_push(path);
}

static void terminal_command_ls(AstraVfs *fs, const char *path, size_t path_length) {
    AstraVfsEntry entries[ASTRA_VFS_MAX_NODES];
    size_t count = 0u;
    const char *effective_path = path_length == 0u ? "." : path;
    const size_t effective_length = path_length == 0u ? 1u : path_length;
    const AstraVfsResult result = astra_vfs_list(
        fs, effective_path, effective_length, entries, ASTRA_VFS_MAX_NODES, &count
    );

    if (result != ASTRA_VFS_OK) {
        terminal_report_vfs(result);
        return;
    }
    if (count == 0u) {
        terminal_history_push("(empty directory)");
    }
    for (size_t index = 0u; index < count && index < ASTRA_VFS_MAX_NODES; index++) {
        char line[DISPLAY_TERMINAL_TEXT_CAP];
        string_copy(line, sizeof line, entries[index].is_directory ? "[DIR] " : "      ");
        string_append(line, sizeof line, entries[index].name);
        if (entries[index].is_directory) {
            string_append(line, sizeof line, "/");
        }
        terminal_history_push(line);
    }
}

static void terminal_command_cat(AstraVfs *fs, const char *path, size_t path_length) {
    char content[ASTRA_VFS_CONTENT_CAP];
    char line[DISPLAY_TERMINAL_TEXT_CAP];
    size_t content_length = 0u;
    size_t line_length = 0u;
    const AstraVfsResult result = astra_vfs_read(
        fs, path, path_length, content, sizeof content, &content_length
    );

    line[0] = '\0';
    if (result != ASTRA_VFS_OK) {
        terminal_report_vfs(result);
        return;
    }
    if (content_length == 0u) {
        terminal_history_push("(empty file)");
        return;
    }
    for (size_t index = 0u; index < content_length; index++) {
        const unsigned char byte = (unsigned char)content[index];
        if (byte == (unsigned char)'\n' || line_length + 1u >= sizeof line) {
            line[line_length] = '\0';
            terminal_history_push(line);
            line_length = 0u;
            line[0] = '\0';
            if (byte == (unsigned char)'\n') {
                continue;
            }
        }
        if (byte >= 0x20u && byte <= 0x7eu) {
            line[line_length] = (char)byte;
        } else if (byte == (unsigned char)'\t') {
            line[line_length] = ' ';
        } else {
            line[line_length] = '.';
        }
        line_length++;
    }
    if (line_length > 0u) {
        line[line_length] = '\0';
        terminal_history_push(line);
    }
}

static void terminal_command_create(
    AstraVfs *fs,
    const char *path,
    size_t path_length,
    bool is_directory
) {
    const AstraVfsResult result = astra_vfs_create(
        fs, path, path_length, is_directory
    );

    if (result != ASTRA_VFS_OK) {
        terminal_report_vfs(result);
    } else {
        terminal_history_push(is_directory ? "directory created" : "file created");
    }
}

static void terminal_execute(void) {
    char command[16];
    char echo[DISPLAY_TERMINAL_TEXT_CAP];
    const size_t line_length = string_length(g_terminal_line);
    const size_t command_end = skip_spaces(g_terminal_line, line_length, 0u);
    size_t command_length = command_end;
    size_t args_start = 0u;
    size_t args_length = 0u;
    const char *args = NULL;
    AstraVfs *fs = astra_system_vfs();

    command[0] = '\0';
    while (command_length < line_length &&
        g_terminal_line[command_length] != ' ' &&
        g_terminal_line[command_length] != '\t') {
        command_length++;
    }
    if (command_length > command_end && command_length - command_end < sizeof command) {
        const size_t used = command_length - command_end;
        for (size_t index = 0u; index < used; index++) {
            command[index] = g_terminal_line[command_end + index];
        }
        command[used] = '\0';
    }
    args_start = skip_spaces(g_terminal_line, line_length, command_length);
    args = &g_terminal_line[args_start];
    args_length = line_length - args_start;

    string_copy(echo, sizeof echo, "$ ");
    string_append(echo, sizeof echo, g_terminal_line);
    terminal_history_push(echo);

    if (string_equals(command, "help")) {
        terminal_history_push("Commands: help uname about clear exit");
        terminal_history_push("Files: pwd ls cd cat whoami");
        terminal_history_push("Changes: touch mkdir write rm");
        terminal_history_push("All filesystem data is RAM-only.");
        astra_uart_puts("desktop: terminal command help\n");
    } else if (string_equals(command, "uname")) {
        terminal_history_push("AstraOS AArch64 / experimental");
        astra_uart_puts("desktop: terminal command uname\n");
    } else if (string_equals(command, "about")) {
        terminal_history_push("AstraOS software desktop prototype");
    } else if (string_equals(command, "pwd")) {
        if (fs == NULL) {
            terminal_history_push("error: filesystem unavailable");
        } else {
            terminal_command_pwd(fs);
        }
        astra_uart_puts("desktop: terminal command pwd\n");
    } else if (string_equals(command, "ls")) {
        if (fs == NULL) {
            terminal_history_push("error: filesystem unavailable");
        } else {
            terminal_command_ls(fs, args, args_length);
        }
        astra_uart_puts("desktop: terminal command ls\n");
    } else if (string_equals(command, "cd")) {
        AstraVfsResult result = ASTRA_VFS_INVALID_ARGUMENT;
        if (fs != NULL && args_length > 0u) {
            result = astra_vfs_chdir(fs, args, args_length);
        }
        if (result != ASTRA_VFS_OK) {
            terminal_report_vfs(result);
        } else {
            terminal_history_push("directory changed");
        }
        astra_uart_puts("desktop: terminal command cd\n");
    } else if (string_equals(command, "cat")) {
        if (fs != NULL && args_length > 0u) {
            terminal_command_cat(fs, args, args_length);
        } else {
            terminal_history_push("usage: cat PATH");
        }
        astra_uart_puts("desktop: terminal command cat\n");
    } else if (string_equals(command, "touch") || string_equals(command, "mkdir")) {
        if (fs != NULL && args_length > 0u) {
            terminal_command_create(fs, args, args_length, string_equals(command, "mkdir"));
        } else {
            terminal_history_push("usage: touch PATH | mkdir PATH");
        }
        astra_uart_puts("desktop: terminal filesystem create\n");
    } else if (string_equals(command, "rm")) {
        AstraVfsResult result = ASTRA_VFS_INVALID_ARGUMENT;
        if (fs != NULL && args_length > 0u) {
            result = astra_vfs_remove(fs, args, args_length);
        }
        if (result != ASTRA_VFS_OK) {
            terminal_report_vfs(result);
        } else {
            terminal_history_push("path removed");
        }
        astra_uart_puts("desktop: terminal command rm\n");
    } else if (string_equals(command, "write")) {
        size_t path_end = 0u;
        size_t text_start = 0u;
        AstraVfsResult result = ASTRA_VFS_INVALID_ARGUMENT;

        while (path_end < args_length && args[path_end] != ' ' && args[path_end] != '\t') {
            path_end++;
        }
        text_start = skip_spaces(args, args_length, path_end);
        if (fs != NULL && path_end > 0u && text_start < args_length) {
            result = astra_vfs_write(
                fs, args, path_end, &args[text_start], args_length - text_start
            );
        }
        if (result != ASTRA_VFS_OK) {
            terminal_history_push("usage: write PATH TEXT");
            if (fs != NULL && path_end > 0u && text_start < args_length) {
                terminal_report_vfs(result);
            }
        } else {
            terminal_history_push("file written");
        }
        astra_uart_puts("desktop: terminal command write\n");
    } else if (string_equals(command, "whoami")) {
        uint32_t uid = 0u;
        if (fs != NULL && astra_vfs_get_uid(fs, &uid) == ASTRA_VFS_OK) {
            terminal_history_push(uid == (uint32_t)ASTRA_VFS_ROOT_UID ? "root" : "astra");
        } else {
            terminal_history_push("unknown");
        }
    } else if (string_equals(command, "clear")) {
        terminal_clear_history();
        astra_uart_puts("desktop: terminal command clear\n");
    } else if (string_equals(command, "exit")) {
        close_surface(find_surface(DISPLAY_APP_TERMINAL));
        astra_uart_puts("desktop: terminal window closed\n");
    } else if (g_terminal_line[0] != '\0') {
        char message[DISPLAY_TERMINAL_TEXT_CAP];
        string_copy(message, sizeof message, "command not found: ");
        string_append(message, sizeof message, command);
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

static bool files_list(AstraVfsEntry *entries, size_t *count_out) {
    AstraVfs *fs = astra_system_vfs();
    const size_t path_length = string_length(g_files_path);
    const AstraVfsResult result = astra_vfs_list(
        fs, g_files_path, path_length, entries, ASTRA_VFS_MAX_NODES, count_out
    );

    if (result != ASTRA_VFS_OK) {
        char message[DISPLAY_TERMINAL_TEXT_CAP];
        string_copy(message, sizeof message, "File list error: ");
        string_append(message, sizeof message, astra_vfs_result_name(result));
        astra_uart_puts(message);
        astra_uart_puts("\n");
        *count_out = 0u;
        return false;
    }
    return true;
}

static bool build_child_path(
    const char *directory,
    const char *name,
    char *path_out,
    size_t path_capacity
) {
    const size_t directory_length = string_length(directory);
    const size_t name_length = string_length(name);
    const bool root = directory_length == 1u && directory[0] == '/';
    const size_t separator_length = root ? 0u : 1u;
    size_t required = directory_length + separator_length + name_length;

    if (directory == NULL || name == NULL || path_out == NULL ||
        path_capacity == 0u || required >= path_capacity) {
        return false;
    }
    if (root) {
        path_out[0] = '/';
        for (size_t index = 0u; index < name_length; index++) {
            path_out[index + 1u] = name[index];
        }
        path_out[name_length + 1u] = '\0';
        return true;
    }
    string_copy(path_out, path_capacity, directory);
    string_append(path_out, path_capacity, "/");
    string_append(path_out, path_capacity, name);
    return true;
}

static void files_open_selected(void) {
    AstraVfsEntry entries[ASTRA_VFS_MAX_NODES];
    size_t count = 0u;
    char target[ASTRA_VFS_PATH_CAP];
    AstraVfs *fs = astra_system_vfs();

    if (fs == NULL || !files_list(entries, &count) || g_files_selection >= count) {
        astra_uart_puts("desktop: no file selected\n");
        return;
    }
    if (!build_child_path(
            g_files_path, entries[g_files_selection].name, target, sizeof target)) {
        astra_uart_puts("desktop: file path too long\n");
        return;
    }
    if (entries[g_files_selection].is_directory) {
        string_copy(g_files_path, sizeof g_files_path, target);
        g_files_selection = 0u;
        g_files_scroll = 0u;
        g_files_last_click_index = SIZE_MAX;
        astra_uart_puts("desktop: files directory opened\n");
        return;
    }

    g_viewer_content_length = 0u;
    {
        const AstraVfsResult result = astra_vfs_read(
            fs, target, string_length(target), g_viewer_content,
            sizeof g_viewer_content - 1u, &g_viewer_content_length
        );
        if (result != ASTRA_VFS_OK || g_viewer_content_length >= sizeof g_viewer_content) {
            astra_uart_puts("desktop: file open failed\n");
            return;
        }
    }
    g_viewer_content[g_viewer_content_length] = '\0';
    string_copy(g_viewer_path, sizeof g_viewer_path, target);
    g_viewer_scroll = 0u;
    g_files_last_click_index = SIZE_MAX;
    open_surface(DISPLAY_APP_VIEWER);
    astra_uart_puts("desktop: text viewer opened: ");
    astra_uart_puts(target);
    astra_uart_puts("\n");
}

static void files_parent_directory(void) {
    size_t length = string_length(g_files_path);
    size_t slash_index = length;

    while (slash_index > 1u && g_files_path[slash_index - 1u] != '/') {
        slash_index--;
    }
    if (slash_index <= 1u) {
        string_copy(g_files_path, sizeof g_files_path, "/");
    } else {
        g_files_path[slash_index - 1u] = '\0';
    }
    g_files_selection = 0u;
    g_files_scroll = 0u;
    astra_uart_puts("desktop: file browser parent directory\n");
}

static bool handle_files_key(uint8_t key) {
    AstraVfsEntry entries[ASTRA_VFS_MAX_NODES];
    size_t count = 0u;

    if (key == (uint8_t)'j' || key == (uint8_t)'J') {
        if (files_list(entries, &count) && count > 0u &&
            g_files_selection + 1u < count) {
            g_files_selection++;
            if (g_files_selection >= g_files_scroll + DISPLAY_FILE_ROWS) {
                g_files_scroll = g_files_selection - DISPLAY_FILE_ROWS + 1u;
            }
        }
        return true;
    }
    if (key == (uint8_t)'k' || key == (uint8_t)'K') {
        if (g_files_selection > 0u) {
            g_files_selection--;
            if (g_files_selection < g_files_scroll) {
                g_files_scroll = g_files_selection;
            }
        }
        return true;
    }
    if (key >= (uint8_t)'1' && key <= (uint8_t)'9') {
        const size_t requested = g_files_scroll + (size_t)(key - (uint8_t)'1');
        if (files_list(entries, &count) && requested < count) {
            g_files_selection = requested;
        }
        return true;
    }
    if (key == (uint8_t)'\r' || key == (uint8_t)'\n') {
        files_open_selected();
        return true;
    }
    if (key == 8u || key == 127u) {
        files_parent_directory();
        return true;
    }
    if (key == (uint8_t)'r' || key == (uint8_t)'R') {
        g_files_selection = 0u;
        g_files_scroll = 0u;
        astra_uart_puts("desktop: files refreshed\n");
        return true;
    }
    if (key == (uint8_t)'/') {
        string_copy(g_files_path, sizeof g_files_path, "/");
        g_files_selection = 0u;
        g_files_scroll = 0u;
        astra_uart_puts("desktop: file browser root\n");
        return true;
    }
    return false;
}

static size_t viewer_line_count(void) {
    size_t lines = g_viewer_content_length == 0u ? 1u : 0u;

    for (size_t index = 0u; index < g_viewer_content_length; index++) {
        if (g_viewer_content[index] == '\n') {
            lines++;
        }
    }
    if (g_viewer_content_length > 0u &&
        g_viewer_content[g_viewer_content_length - 1u] != '\n') {
        lines++;
    }
    return lines;
}

static bool handle_viewer_key(uint8_t key) {
    const size_t lines = viewer_line_count();

    if (key == (uint8_t)'j' || key == (uint8_t)'J') {
        if (g_viewer_scroll + 1u < lines) {
            g_viewer_scroll++;
        }
        return true;
    }
    if (key == (uint8_t)'k' || key == (uint8_t)'K') {
        if (g_viewer_scroll > 0u) {
            g_viewer_scroll--;
        }
        return true;
    }
    if (key == 8u || key == 127u || key == (uint8_t)'b' || key == (uint8_t)'B') {
        close_surface(find_surface(DISPLAY_APP_VIEWER));
        focus_surface(find_surface(DISPLAY_APP_FILES));
        astra_uart_puts("desktop: text viewer closed\n");
        return true;
    }
    return false;
}

static void render_terminal(const AstraDisplaySurface *surface) {
    const uint32_t left = surface->x + 18u;
    const uint32_t top = surface->y + DISPLAY_TITLEBAR_HEIGHT + 17u;
    uint32_t rows_available = surface->height > 128u ?
        (surface->height - 128u) / 18u : 1u;
    uint32_t row_y = top;

    astra_framebuffer_fill_rect(
        surface->x + 12u, surface->y + DISPLAY_TITLEBAR_HEIGHT + 8u,
        surface->width - 24u, surface->height - DISPLAY_TITLEBAR_HEIGHT - 18u,
        COLOR_TERMINAL
    );
    draw_text(left, row_y, "AstraOS shell - shared RAM filesystem", COLOR_ACCENT);
    row_y += 24u;
    if (rows_available > DISPLAY_TERMINAL_HISTORY) {
        rows_available = DISPLAY_TERMINAL_HISTORY;
    }
    for (uint32_t row = 0u; row < rows_available; row++) {
        const size_t history_index =
            DISPLAY_TERMINAL_HISTORY - (size_t)rows_available + (size_t)row;
        if (g_terminal_history[history_index][0] != '\0') {
            draw_text(
                left, row_y, g_terminal_history[history_index], COLOR_TERMINAL_TEXT
            );
        }
        row_y += 18u;
    }
    draw_text(left, surface->y + surface->height - 48u, "$", COLOR_ACCENT);
    draw_text(
        left + 3u * DISPLAY_TEXT_CELL_WIDTH,
        surface->y + surface->height - 48u,
        g_terminal_line, COLOR_TERMINAL_TEXT
    );
    draw_text(
        left, surface->y + surface->height - 26u,
        "help lists commands; Enter runs", COLOR_MUTED
    );
}

static void render_files(const AstraDisplaySurface *surface) {
    AstraVfsEntry entries[ASTRA_VFS_MAX_NODES];
    size_t count = 0u;
    uint32_t row_y = surface->y + DISPLAY_TITLEBAR_HEIGHT + 18u;
    uint32_t rows_available = surface->height > 116u ?
        (surface->height - 116u) / 18u : 1u;
    char path_label[DISPLAY_TERMINAL_TEXT_CAP];

    astra_framebuffer_fill_rect(
        surface->x + 10u, surface->y + DISPLAY_TITLEBAR_HEIGHT + 8u,
        surface->width - 20u, surface->height - DISPLAY_TITLEBAR_HEIGHT - 18u,
        COLOR_TERMINAL
    );
    string_copy(path_label, sizeof path_label, g_files_path);
    draw_text(surface->x + 18u, row_y, path_label, COLOR_ACCENT);
    row_y += 23u;
    if (rows_available > DISPLAY_FILE_ROWS) {
        rows_available = DISPLAY_FILE_ROWS;
    }

    if (!files_list(entries, &count)) {
        draw_text(surface->x + 18u, row_y, "Could not read this directory.", COLOR_WARNING);
        return;
    }
    if (count == 0u) {
        draw_text(surface->x + 18u, row_y, "(empty directory)", COLOR_MUTED);
    }
    if (g_files_selection >= count && count > 0u) {
        g_files_selection = count - 1u;
    }
    if (g_files_scroll > g_files_selection) {
        g_files_scroll = g_files_selection;
    }
    for (uint32_t row = 0u; row < rows_available; row++) {
        const size_t entry_index = g_files_scroll + (size_t)row;
        char line[DISPLAY_TERMINAL_TEXT_CAP];

        if (entry_index >= count) {
            break;
        }
        if (entry_index == g_files_selection) {
            astra_framebuffer_fill_rect(
                surface->x + 13u, row_y - 3u, surface->width - 26u, 17u,
                COLOR_SELECTION
            );
            string_copy(line, sizeof line, "> ");
        } else {
            string_copy(line, sizeof line, "  ");
        }
        string_append(line, sizeof line, entries[entry_index].is_directory ? "[DIR] " : "      ");
        string_append(line, sizeof line, entries[entry_index].name);
        if (entries[entry_index].is_directory) {
            string_append(line, sizeof line, "/");
        }
        draw_text(surface->x + 18u, row_y, line, COLOR_TEXT);
        row_y += 18u;
    }
    draw_text(
        surface->x + 18u, surface->y + surface->height - 26u,
        "j/k select  1-9 jump  Enter open  Backspace up  R refresh", COLOR_MUTED
    );
}

static void render_viewer(const AstraDisplaySurface *surface) {
    uint32_t row_y = surface->y + DISPLAY_TITLEBAR_HEIGHT + 18u;
    size_t line_number = 0u;
    size_t position = 0u;
    uint32_t rows_available = surface->height > 100u ?
        (surface->height - 100u) / 16u : 1u;

    astra_framebuffer_fill_rect(
        surface->x + 10u, surface->y + DISPLAY_TITLEBAR_HEIGHT + 8u,
        surface->width - 20u, surface->height - DISPLAY_TITLEBAR_HEIGHT - 18u,
        COLOR_TERMINAL
    );
    draw_text(surface->x + 18u, row_y, g_viewer_path, COLOR_ACCENT);
    row_y += 22u;
    if (rows_available > DISPLAY_VIEWER_ROWS) {
        rows_available = DISPLAY_VIEWER_ROWS;
    }
    while (position < g_viewer_content_length &&
        rows_available > 0u && line_number < g_viewer_scroll) {
        if (g_viewer_content[position] == '\n') {
            line_number++;
        }
        position++;
    }
    if (g_viewer_content_length == 0u) {
        draw_text(surface->x + 18u, row_y, "(empty file)", COLOR_MUTED);
    }
    for (uint32_t row = 0u; row < rows_available && position < g_viewer_content_length; row++) {
        char line[DISPLAY_TERMINAL_TEXT_CAP];
        size_t used = 0u;

        while (position < g_viewer_content_length &&
            g_viewer_content[position] != '\n' &&
            used + 1u < sizeof line) {
            const unsigned char byte = (unsigned char)g_viewer_content[position];
            if (byte >= 0x20u && byte <= 0x7eu) {
                line[used] = (char)byte;
            } else if (byte == (unsigned char)'\t') {
                line[used] = ' ';
            } else {
                line[used] = '.';
            }
            used++;
            position++;
        }
        if (position < g_viewer_content_length &&
            g_viewer_content[position] == '\n') {
            position++;
        }
        line[used] = '\0';
        draw_text(surface->x + 18u, row_y, line, COLOR_TERMINAL_TEXT);
        row_y += 16u;
    }
    draw_text(
        surface->x + 18u, surface->y + surface->height - 26u,
        "j/k scroll  Backspace or B returns to Files", COLOR_MUTED
    );
}

static void render_window_content(const AstraDisplaySurface *surface) {
    const uint32_t left = surface->x + 18u;
    const uint32_t top = surface->y + DISPLAY_TITLEBAR_HEIGHT + 18u;

    switch (surface->app) {
        case DISPLAY_APP_WELCOME:
            draw_text(left, top, "Welcome to AstraOS", COLOR_ACCENT);
            draw_text(left, top + 31u, "A lightweight AArch64 desktop environment.", COLOR_TEXT);
            draw_text(left, top + 53u, "A panel, task list, dock and window manager are active.", COLOR_TEXT);
            draw_text(left, top + 88u, "Open an essential tool:", COLOR_MUTED);
            draw_text(left + 10u, top + 116u, "T  Terminal and shell commands", COLOR_TERMINAL_TEXT);
            draw_text(left + 10u, top + 140u, "F  File Manager and text viewer", COLOR_GREEN);
            draw_text(left + 10u, top + 164u, "M  Applications menu", COLOR_BLUE);
            draw_text(left, surface->y + surface->height - 42u, "C close  N minimise  X maximise  W switch windows", COLOR_WARNING);
            break;
        case DISPLAY_APP_TERMINAL:
            render_terminal(surface);
            break;
        case DISPLAY_APP_FILES:
            render_files(surface);
            break;
        case DISPLAY_APP_ABOUT:
            draw_text(left, top, "AstraOS", COLOR_ACCENT);
            draw_text(left, top + 31u, "Experimental freestanding AArch64 OS", COLOR_TEXT);
            draw_text(left, top + 55u, "Software-rendered GOP framebuffer desktop", COLOR_TEXT);
            draw_text(left, top + 79u, "Window focus, tasks and maximise/minimise controls", COLOR_TEXT);
            draw_text(left, top + 103u, "A shared fixed-size RAM filesystem backs Files and Terminal", COLOR_TEXT);
            draw_text(left, top + 139u, "Pointer uses available UEFI protocols.", COLOR_WARNING);
            draw_text(left, top + 163u, "Native USB/HID and userspace processes are not implemented.", COLOR_MUTED);
            break;
        case DISPLAY_APP_VIEWER:
            render_viewer(surface);
            break;
        default:
            draw_text(left, top, "Unknown application", COLOR_WARNING);
            break;
    }
}

static void render_surface(const AstraDisplaySurface *surface) {
    uint32_t title_color = 0u;
    uint32_t right = 0u;

    if (surface == NULL || !surface->visible) {
        return;
    }
    title_color = surface->focused ? COLOR_TITLE_ACTIVE : COLOR_TITLE_IDLE;
    right = surface->x + surface->width;
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
        surface->x + 14u, surface->y + 11u,
        app_title(surface->app), COLOR_TEXT
    );

    astra_framebuffer_fill_rect(right - 23u, surface->y + 9u, 16u, 14u, COLOR_CLOSE);
    astra_framebuffer_fill_rect(right - 19u, surface->y + 13u, 8u, 2u, COLOR_PANEL);
    astra_framebuffer_fill_rect(right - 47u, surface->y + 9u, 16u, 14u, COLOR_BLUE);
    astra_framebuffer_fill_rect(right - 43u, surface->y + 13u, 8u, 6u, COLOR_PANEL);
    astra_framebuffer_fill_rect(right - 71u, surface->y + 9u, 16u, 14u, COLOR_MINIMIZE);
    astra_framebuffer_fill_rect(right - 67u, surface->y + 18u, 8u, 2u, COLOR_PANEL);
    render_window_content(surface);
}

static void render_applications_menu(void) {
    const uint32_t x = 12u;
    const uint32_t y = DISPLAY_PANEL_HEIGHT + 5u;
    const uint32_t width = 220u;
    const uint32_t height = 150u;

    astra_framebuffer_fill_rect(x + 4u, y + 4u, width, height, COLOR_PANEL);
    astra_framebuffer_fill_rect(x, y, width, height, COLOR_WINDOW_EDGE);
    astra_framebuffer_fill_rect(x + 2u, y + 2u, width - 4u, height - 4u, COLOR_PANEL);
    draw_text(x + 12u, y + 12u, "AstraOS Start menu", COLOR_ACCENT);
    astra_framebuffer_fill_rect(x + 8u, y + 31u, width - 16u, 1u, COLOR_PANEL_EDGE);
    draw_text(x + 14u, y + 43u, "T  Terminal", COLOR_TEXT);
    draw_text(x + 14u, y + 67u, "F  File Manager", COLOR_TEXT);
    draw_text(x + 14u, y + 91u, "A  About AstraOS", COLOR_TEXT);
    draw_text(x + 14u, y + 119u, "Q  Return to shell", COLOR_WARNING);
}


static void draw_pointer_cursor(void) {
    static const uint8_t row_widths[] = {
        2u, 4u, 6u, 8u, 10u, 12u, 14u, 16u, 18u, 20u, 22u, 14u, 12u, 10u, 8u, 6u
    };

    for (size_t row = 0u; row < sizeof row_widths / sizeof row_widths[0]; row++) {
        const uint32_t y = g_mouse_y + (uint32_t)row;
        const uint32_t width = (uint32_t)row_widths[row];

        if (g_mouse_x >= g_screen_width || y >= g_screen_height) {
            break;
        }
        astra_framebuffer_fill_rect(g_mouse_x, y, width, 1u, UINT32_C(0x05080c));
        if (width > 3u && g_mouse_x + 2u < g_screen_width) {
            astra_framebuffer_fill_rect(
                g_mouse_x + 2u, y, width - 3u, 1u, UINT32_C(0xf7f9fc)
            );
        }
    }
}

static void render_desktop(void) {
    bool rendered[DISPLAY_SURFACE_CAP];

    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        rendered[index] = false;
    }
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
    if (g_menu_open) {
        render_applications_menu();
    }
    draw_pointer_cursor();
}

static void stop_display_server(void) {
    g_menu_open = false;
    astra_framebuffer_set_console_enabled(true);
    astra_framebuffer_clear();
    astra_uart_puts("desktop: returned to shell\n");
}


static uint32_t scale_absolute_axis(
    uint64_t value,
    uint64_t minimum,
    uint64_t maximum,
    uint32_t pixels
) {
    uint64_t offset = 0u;
    uint64_t span = 0u;
    uint64_t scaled = 0u;

    if (pixels <= 1u || maximum <= minimum || value <= minimum) {
        return 0u;
    }
    if (value >= maximum) {
        return pixels - 1u;
    }
    offset = value - minimum;
    span = maximum - minimum;
    scaled = offset * (uint64_t)(pixels - 1u) / span;
    if (scaled >= (uint64_t)pixels) {
        return pixels - 1u;
    }
    return (uint32_t)scaled;
}

static size_t surface_index(const AstraDisplaySurface *surface) {
    if (surface == NULL) {
        return DISPLAY_SURFACE_CAP;
    }
    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        if (&g_surfaces[index] == surface) {
            return index;
        }
    }
    return DISPLAY_SURFACE_CAP;
}

static void restore_surface_from_taskbar(AstraDisplaySurface *surface) {
    if (surface == NULL) {
        return;
    }
    surface->visible = true;
    surface->minimized = false;
    focus_surface(surface);
    astra_uart_puts("desktop: taskbar window focused\n");
}

static void move_dragged_window(int64_t delta_x, int64_t delta_y) {
    AstraDisplaySurface *surface = NULL;
    int64_t new_x = 0;
    int64_t new_y = 0;
    uint32_t max_x = 0u;
    uint32_t max_y = 0u;

    if (g_drag_surface_index < 0 ||
        (size_t)g_drag_surface_index >= DISPLAY_SURFACE_CAP ||
        !g_mouse_left_down) {
        return;
    }
    surface = &g_surfaces[(size_t)g_drag_surface_index];
    if (!surface->visible || surface->maximized) {
        return;
    }
    new_x = (int64_t)surface->x + delta_x;
    new_y = (int64_t)surface->y + delta_y;
    max_x = g_screen_width > surface->width ? g_screen_width - surface->width : 0u;
    max_y = g_screen_height > DISPLAY_DOCK_HEIGHT + surface->height ?
        g_screen_height - DISPLAY_DOCK_HEIGHT - surface->height : DISPLAY_PANEL_HEIGHT;
    if (new_x < 0) {
        new_x = 0;
    }
    if (new_y < (int64_t)DISPLAY_PANEL_HEIGHT) {
        new_y = (int64_t)DISPLAY_PANEL_HEIGHT;
    }
    if (new_x > (int64_t)max_x) {
        new_x = (int64_t)max_x;
    }
    if (new_y > (int64_t)max_y) {
        new_y = (int64_t)max_y;
    }
    surface->x = (uint32_t)new_x;
    surface->y = (uint32_t)new_y;
}

static AstraDisplaySurface *surface_at(uint32_t x, uint32_t y) {
    AstraDisplaySurface *selected = NULL;
    uint32_t highest_z = 0u;

    for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
        AstraDisplaySurface *surface = &g_surfaces[index];
        if (!surface->visible || surface->z_order <= highest_z ||
            x < surface->x || y < surface->y ||
            x - surface->x >= surface->width || y - surface->y >= surface->height) {
            continue;
        }
        selected = surface;
        highest_z = surface->z_order;
    }
    return selected;
}

static bool handle_menu_pointer_press(uint32_t x, uint32_t y, bool *handled_out) {
    const uint32_t menu_x = 12u;
    const uint32_t menu_y = DISPLAY_PANEL_HEIGHT + 5u;
    const uint32_t menu_width = 220u;
    const uint32_t menu_height = 150u;

    if (handled_out == NULL) {
        return false;
    }
    *handled_out = false;
    if (!g_menu_open) {
        return false;
    }
    if (x >= menu_x && x < menu_x + menu_width &&
        y >= menu_y && y < menu_y + menu_height) {
        const uint32_t row = y - menu_y;
        *handled_out = true;
        g_menu_open = false;
        if (row >= 34u && row < 59u) {
            open_surface(DISPLAY_APP_TERMINAL);
            astra_uart_puts("desktop: terminal window opened\n");
        } else if (row >= 59u && row < 83u) {
            open_surface(DISPLAY_APP_FILES);
            astra_uart_puts("desktop: file browser opened\n");
        } else if (row >= 83u && row < 108u) {
            open_surface(DISPLAY_APP_ABOUT);
            astra_uart_puts("desktop: about window focused\n");
        } else if (row >= 108u && row < menu_height) {
            stop_display_server();
            return true;
        }
        return false;
    }
    g_menu_open = false;
    return false;
}


static void handle_files_pointer_press(
    const AstraDisplaySurface *surface,
    uint32_t x,
    uint32_t y
) {
    AstraVfsEntry entries[ASTRA_VFS_MAX_NODES];
    size_t count = 0u;
    size_t row = 0u;
    size_t selected_index = 0u;
    uint32_t first_row_y = 0u;
    const uint32_t content_left = surface->x + 13u;
    const uint32_t content_right = surface->x + surface->width - 13u;

    if (surface == NULL || x < content_left || x >= content_right) {
        return;
    }
    first_row_y = surface->y + DISPLAY_TITLEBAR_HEIGHT + 18u + 23u;
    if (y < first_row_y) {
        return;
    }
    row = (size_t)((y - first_row_y) / 18u);
    if (row >= (size_t)DISPLAY_FILE_ROWS) {
        return;
    }
    if (!files_list(entries, &count) || count == 0u) {
        return;
    }
    selected_index = g_files_scroll + row;
    if (selected_index >= count) {
        return;
    }

    if (selected_index == g_files_last_click_index &&
        (uint32_t)(g_input_event_counter - g_files_last_click_event) <=
            (uint32_t)DISPLAY_DOUBLE_CLICK_EVENT_LIMIT) {
        g_files_selection = selected_index;
        g_files_last_click_index = SIZE_MAX;
        files_open_selected();
        return;
    }

    g_files_selection = selected_index;
    g_files_last_click_index = selected_index;
    g_files_last_click_event = g_input_event_counter;
    astra_uart_puts("desktop: file entry selected by pointer\n");
}

static bool handle_mouse_press(uint32_t x, uint32_t y) {
    AstraDisplaySurface *surface = NULL;
    bool menu_handled = false;
    const uint32_t panel_right_edge =
        g_screen_width > 126u ? g_screen_width - 126u : 0u;

    if (handle_menu_pointer_press(x, y, &menu_handled)) {
        return true;
    }
    if (menu_handled) {
        return false;
    }
    if (y < DISPLAY_PANEL_HEIGHT) {
        if (x < 145u) {
            g_menu_open = !g_menu_open;
            astra_uart_puts(g_menu_open ?
                "desktop: applications menu opened\n" : "desktop: applications menu closed\n");
            return false;
        }
        {
            uint32_t task_x = 152u;
            for (size_t index = 0u; index < DISPLAY_SURFACE_CAP; index++) {
                AstraDisplaySurface *candidate = &g_surfaces[index];
                if (!candidate->visible && !candidate->minimized) {
                    continue;
                }
                if (task_x + 96u > panel_right_edge) {
                    break;
                }
                if (x >= task_x && x < task_x + 93u) {
                    restore_surface_from_taskbar(candidate);
                    return false;
                }
                task_x += 96u;
            }
        }
        return false;
    }

    surface = surface_at(x, y);
    if (surface != NULL) {
        const size_t index = surface_index(surface);
        const uint32_t right = surface->x + surface->width;
        const uint32_t title_y = surface->y + DISPLAY_TITLEBAR_HEIGHT;

        focus_surface(surface);
        if (y < title_y) {
            if (x >= right - 23u && x < right - 7u) {
                if (surface->app == DISPLAY_APP_TERMINAL) {
                    astra_uart_puts("desktop: terminal window closed\n");
                }
                close_surface(surface);
                astra_uart_puts("desktop: window closed\n");
            } else if (x >= right - 47u && x < right - 31u) {
                toggle_maximize_surface(surface);
            } else if (x >= right - 71u && x < right - 55u) {
                minimize_surface(surface);
            } else if (index < DISPLAY_SURFACE_CAP) {
                g_drag_surface_index = (int)index;
            }
        } else if (surface->app == DISPLAY_APP_FILES) {
            handle_files_pointer_press(surface, x, y);
        } else if (surface->app == DISPLAY_APP_VIEWER) {
            astra_uart_puts("desktop: text viewer focused by pointer\n");
        }
        return false;
    }

    if (x >= 18u && x <= 80u) {
        if (y >= 62u && y < 120u) {
            open_surface(DISPLAY_APP_TERMINAL);
            astra_uart_puts("desktop: terminal window opened\n");
        } else if (y >= 130u && y < 187u) {
            open_surface(DISPLAY_APP_FILES);
            astra_uart_puts("desktop: file browser opened\n");
        } else if (y >= 198u && y < 255u) {
            open_surface(DISPLAY_APP_ABOUT);
            astra_uart_puts("desktop: about window focused\n");
        }
        return false;
    }

    if (y >= g_screen_height - DISPLAY_DOCK_HEIGHT) {
        if (x < 92u) {
            open_surface(DISPLAY_APP_TERMINAL);
            astra_uart_puts("desktop: terminal window opened\n");
        } else if (x < 160u) {
            open_surface(DISPLAY_APP_FILES);
            astra_uart_puts("desktop: file browser opened\n");
        } else if (x < 240u) {
            open_surface(DISPLAY_APP_ABOUT);
            astra_uart_puts("desktop: about window focused\n");
        }
    }
    return false;
}

static bool handle_pointer_event(const AstraInputEvent *event) {
    uint32_t old_x = g_mouse_x;
    uint32_t old_y = g_mouse_y;
    bool was_left_down = g_mouse_left_down;

    if (event == NULL || event->kind != ASTRA_INPUT_EVENT_POINTER) {
        return false;
    }
    if (!g_logged_pointer_event) {
        g_logged_pointer_event = true;
        astra_uart_puts("desktop: pointer event received\n");
    }
    if (event->absolute) {
        g_mouse_x = scale_absolute_axis(
            event->absolute_x, event->min_x, event->max_x, g_screen_width
        );
        g_mouse_y = scale_absolute_axis(
            event->absolute_y, event->min_y, event->max_y, g_screen_height
        );
    } else {
        int64_t new_x = (int64_t)g_mouse_x + (int64_t)event->delta_x * 2;
        int64_t new_y = (int64_t)g_mouse_y + (int64_t)event->delta_y * 2;
        if (new_x < 0) {
            new_x = 0;
        } else if (new_x >= (int64_t)g_screen_width) {
            new_x = (int64_t)g_screen_width - 1;
        }
        if (new_y < 0) {
            new_y = 0;
        } else if (new_y >= (int64_t)g_screen_height) {
            new_y = (int64_t)g_screen_height - 1;
        }
        g_mouse_x = (uint32_t)new_x;
        g_mouse_y = (uint32_t)new_y;
    }

    if (g_pointer_debug_reports < 8u) {
        g_pointer_debug_reports++;
        astra_uart_puts("desktop: pointer report screen=");
        astra_uart_put_u32(g_mouse_x);
        astra_uart_puts(",");
        astra_uart_put_u32(g_mouse_y);
        astra_uart_puts(" left=");
        astra_uart_put_u32(event->left_button_down ? 1u : 0u);
        astra_uart_puts(" abs=");
        astra_uart_put_u32(event->absolute ? 1u : 0u);
        if (event->absolute) {
            astra_uart_puts(" raw=");
            astra_uart_put_u32((uint32_t)event->absolute_x);
            astra_uart_puts(",");
            astra_uart_put_u32((uint32_t)event->absolute_y);
            astra_uart_puts(" range=");
            astra_uart_put_u32((uint32_t)event->max_x);
            astra_uart_puts(",");
            astra_uart_put_u32((uint32_t)event->max_y);
        } else {
            astra_uart_puts(" delta=");
            astra_uart_put_u32((uint32_t)event->delta_x);
            astra_uart_puts(",");
            astra_uart_put_u32((uint32_t)event->delta_y);
        }
        astra_uart_puts("\\n");
    }

    if (event->left_button_down && !was_left_down) {
        if (handle_mouse_press(g_mouse_x, g_mouse_y)) {
            g_mouse_left_down = false;
            g_drag_surface_index = -1;
            return true;
        }
    } else if (event->left_button_down && was_left_down) {
        move_dragged_window(
            (int64_t)g_mouse_x - (int64_t)old_x,
            (int64_t)g_mouse_y - (int64_t)old_y
        );
    } else {
        g_drag_surface_index = -1;
    }
    g_mouse_left_down = event->left_button_down;
    return false;
}


static bool handle_menu_key(uint8_t key) {
    if (!g_menu_open) {
        return false;
    }
    if (key == (uint8_t)'m' || key == (uint8_t)'M' || key == 27u) {
        g_menu_open = false;
        astra_uart_puts("desktop: applications menu closed\n");
        return true;
    }
    if (key == (uint8_t)'t' || key == (uint8_t)'T' || key == (uint8_t)'1') {
        g_menu_open = false;
        open_surface(DISPLAY_APP_TERMINAL);
        astra_uart_puts("desktop: terminal window opened\n");
        return true;
    }
    if (key == (uint8_t)'f' || key == (uint8_t)'F' || key == (uint8_t)'2') {
        g_menu_open = false;
        open_surface(DISPLAY_APP_FILES);
        astra_uart_puts("desktop: file browser opened\n");
        return true;
    }
    if (key == (uint8_t)'a' || key == (uint8_t)'A' || key == (uint8_t)'3') {
        g_menu_open = false;
        open_surface(DISPLAY_APP_ABOUT);
        astra_uart_puts("desktop: about window focused\n");
        return true;
    }
    if (key == (uint8_t)'q' || key == (uint8_t)'Q') {
        stop_display_server();
        return true;
    }
    g_menu_open = false;
    return false;
}

bool astra_display_server_run(void) {
    uint8_t key = 0u;
    AstraInputEvent event;

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
        g_surfaces[index].restore_x = 0u;
        g_surfaces[index].restore_y = 0u;
        g_surfaces[index].restore_width = 0u;
        g_surfaces[index].restore_height = 0u;
        g_surfaces[index].z_order = 0u;
        g_surfaces[index].visible = false;
        g_surfaces[index].focused = false;
        g_surfaces[index].minimized = false;
        g_surfaces[index].maximized = false;
    }
    g_next_z_order = 1u;
    g_terminal_line[0] = '\0';
    terminal_clear_history();
    string_copy(g_files_path, sizeof g_files_path, "/");
    g_files_selection = 0u;
    g_files_scroll = 0u;
    g_viewer_path[0] = '\0';
    g_viewer_content[0] = '\0';
    g_viewer_content_length = 0u;
    g_viewer_scroll = 0u;
    g_menu_open = false;
    g_mouse_x = g_screen_width / 2u;
    g_mouse_y = g_screen_height / 2u;
    g_mouse_left_down = false;
    g_drag_surface_index = -1;
    g_logged_pointer_event = false;
    g_pointer_debug_reports = 0u;
    g_input_event_counter = 0u;
    g_files_last_click_event = 0u;
    g_files_last_click_index = SIZE_MAX;

    astra_framebuffer_set_console_enabled(false);
    open_surface(DISPLAY_APP_WELCOME);
    render_desktop();
    astra_uart_puts("ASTRAOS_DISPLAY_SERVER_READY\n");
    astra_uart_puts("ASTRAOS_DESKTOP_RENDERED\n");
    astra_uart_puts("Desktop: T terminal, F files, A about, M menu, W switch window, ");
    astra_uart_puts("C close, N minimise, X maximise, Q shell.\n");
    astra_uart_puts(astra_input_pointer_available() ?
        "Desktop pointer: UEFI mouse input available.\n" :
        "Desktop pointer: unavailable; keyboard controls remain enabled.\n");

    for (;;) {
        AstraDisplaySurface *focus = NULL;

        if (!astra_input_read_event(&event)) {
            astra_uart_puts("Display server input failed; returning to the shell.\n");
            stop_display_server();
            return false;
        }
        if (g_input_event_counter == UINT32_MAX) {
            g_input_event_counter = 0u;
            g_files_last_click_index = SIZE_MAX;
            g_files_last_click_event = 0u;
        } else {
            g_input_event_counter++;
        }
        if (event.kind == ASTRA_INPUT_EVENT_POINTER) {
            if (handle_pointer_event(&event)) {
                return true;
            }
            render_desktop();
            continue;
        }
        key = event.key;

        if (g_menu_open && (key == (uint8_t)'q' || key == (uint8_t)'Q')) {
            stop_display_server();
            return true;
        }
        if (handle_menu_key(key)) {
            render_desktop();
            continue;
        }

        focus = focused_surface();
        if (focus != NULL && focus->app == DISPLAY_APP_TERMINAL) {
            handle_terminal_key(key);
            render_desktop();
            continue;
        }
        if (focus != NULL && focus->app == DISPLAY_APP_FILES && handle_files_key(key)) {
            render_desktop();
            continue;
        }
        if (focus != NULL && focus->app == DISPLAY_APP_VIEWER && handle_viewer_key(key)) {
            render_desktop();
            continue;
        }

        switch (key) {
            case 't':
            case 'T':
            case '1':
                g_menu_open = false;
                open_surface(DISPLAY_APP_TERMINAL);
                astra_uart_puts("desktop: terminal window opened\n");
                break;
            case 'f':
            case 'F':
            case '2':
                g_menu_open = false;
                open_surface(DISPLAY_APP_FILES);
                astra_uart_puts("desktop: file browser opened\n");
                break;
            case 'a':
            case 'A':
            case '3':
                g_menu_open = false;
                open_surface(DISPLAY_APP_ABOUT);
                astra_uart_puts("desktop: about window focused\n");
                break;
            case 'm':
            case 'M':
                g_menu_open = true;
                astra_uart_puts("desktop: applications menu opened\n");
                break;
            case 'c':
            case 'C':
                if (focus != NULL && focus->app == DISPLAY_APP_TERMINAL) {
                    astra_uart_puts("desktop: terminal window closed\n");
                }
                close_surface(focus);
                astra_uart_puts("desktop: window closed\n");
                break;
            case 'n':
            case 'N':
                minimize_surface(focus);
                break;
            case 'x':
            case 'X':
                toggle_maximize_surface(focus);
                break;
            case 'w':
            case 'W':
                focus_next_surface();
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
