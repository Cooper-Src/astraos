#include "desktop.h"

#include "display_server.h"
#include "framebuffer.h"
#include "input.h"
#include "uart.h"

#include <stddef.h>
#include <stdint.h>

enum {
    DESKTOP_LOG_LINES = 20u,
    DESKTOP_LINE_CAP = 96u,
    DESKTOP_INPUT_CAP = 128u,
    DESKTOP_TEXT_X = 8u,
    DESKTOP_HEADER_Y = 4u,
    DESKTOP_HISTORY_Y = 24u,
    DESKTOP_LINE_HEIGHT = 16u,
    DESKTOP_INPUT_BOTTOM_GAP = 8u,
    DESKTOP_SMALL_WINDOW_WIDTH = 420u,
    DESKTOP_LARGE_WINDOW_WIDTH = 620u,
    DESKTOP_SMALL_WINDOW_HEIGHT = 300u,
    DESKTOP_LARGE_WINDOW_HEIGHT = 380u
};

#define DESKTOP_COLOR_TERMINAL_BG UINT32_C(0x0a111b)
#define DESKTOP_COLOR_TERMINAL_FG UINT32_C(0x91e6b3)
#define DESKTOP_COLOR_TEXT UINT32_C(0xf1f5f9)
#define DESKTOP_COLOR_MUTED UINT32_C(0x9fb0c5)
#define DESKTOP_COLOR_ACCENT UINT32_C(0x70b7ff)
#define DESKTOP_COLOR_FILE UINT32_C(0xc4d7ee)
#define DESKTOP_COLOR_DIRECTORY UINT32_C(0x75bfff)

typedef struct DesktopTerminal {
    AstraVfs *filesystem;
    char lines[DESKTOP_LOG_LINES][DESKTOP_LINE_CAP];
    size_t line_lengths[DESKTOP_LOG_LINES];
    size_t first_line;
    size_t line_count;
    char input[DESKTOP_INPUT_CAP];
    size_t input_length;
} DesktopTerminal;

static DesktopTerminal g_terminal;
static uint32_t g_terminal_window_id;
static uint32_t g_system_window_id;
static uint32_t g_files_window_id;

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

static bool text_equals(const char *left, const char *right) {
    size_t i = 0u;
    if (left == NULL || right == NULL) {
        return false;
    }
    while (left[i] != '\0' && right[i] != '\0' && left[i] == right[i]) {
        i++;
    }
    return left[i] == '\0' && right[i] == '\0';
}

static bool span_equals_text(const char *span, size_t span_length, const char *text) {
    size_t i = 0u;
    if (span == NULL || text == NULL) {
        return false;
    }
    while (i < span_length && text[i] != '\0' && span[i] == text[i]) {
        i++;
    }
    return i == span_length && text[i] == '\0';
}

static size_t skip_spaces(const char *text, size_t length, size_t position) {
    while (position < length && (text[position] == ' ' || text[position] == '\t')) {
        position++;
    }
    return position;
}

static void terminal_newline(DesktopTerminal *terminal) {
    size_t index = 0u;
    if (terminal == NULL) {
        return;
    }
    if (terminal->line_count < (size_t)DESKTOP_LOG_LINES) {
        index = (terminal->first_line + terminal->line_count) %
            (size_t)DESKTOP_LOG_LINES;
        terminal->line_count++;
    } else {
        terminal->first_line = (terminal->first_line + 1u) %
            (size_t)DESKTOP_LOG_LINES;
        index = (terminal->first_line + terminal->line_count - 1u) %
            (size_t)DESKTOP_LOG_LINES;
    }
    terminal->line_lengths[index] = 0u;
    terminal->lines[index][0] = '\0';
}

static void terminal_reset(DesktopTerminal *terminal) {
    if (terminal == NULL) {
        return;
    }
    for (size_t i = 0u; i < (size_t)DESKTOP_LOG_LINES; i++) {
        terminal->line_lengths[i] = 0u;
        terminal->lines[i][0] = '\0';
    }
    terminal->first_line = 0u;
    terminal->line_count = 0u;
    terminal_newline(terminal);
}

static void terminal_put_byte(DesktopTerminal *terminal, uint8_t value) {
    size_t index = 0u;
    size_t length = 0u;

    if (terminal == NULL) {
        return;
    }
    if (value == (uint8_t)'\r') {
        return;
    }
    if (value == (uint8_t)'\n') {
        terminal_newline(terminal);
        return;
    }
    if (value < 0x20u || value > 0x7eu) {
        value = (uint8_t)'?';
    }
    index = (terminal->first_line + terminal->line_count - 1u) %
        (size_t)DESKTOP_LOG_LINES;
    length = terminal->line_lengths[index];
    if (length + 1u >= (size_t)DESKTOP_LINE_CAP) {
        terminal_newline(terminal);
        index = (terminal->first_line + terminal->line_count - 1u) %
            (size_t)DESKTOP_LOG_LINES;
        length = terminal->line_lengths[index];
    }
    terminal->lines[index][length] = (char)value;
    terminal->lines[index][length + 1u] = '\0';
    terminal->line_lengths[index] = length + 1u;
}

static void terminal_write(DesktopTerminal *terminal, const char *text, size_t length) {
    if (terminal == NULL || text == NULL) {
        return;
    }
    for (size_t i = 0u; i < length; i++) {
        terminal_put_byte(terminal, (uint8_t)text[i]);
    }
}

static void terminal_write_text(DesktopTerminal *terminal, const char *text) {
    terminal_write(terminal, text, text_length(text));
}

static void terminal_write_result(DesktopTerminal *terminal, AstraVfsResult result) {
    terminal_write_text(terminal, "error: ");
    terminal_write_text(terminal, astra_vfs_result_name(result));
    terminal_put_byte(terminal, (uint8_t)'\n');
}

static void terminal_command_ls(DesktopTerminal *terminal, const char *path) {
    AstraVfsEntry entries[ASTRA_VFS_MAX_NODES];
    size_t count = 0u;
    const char *chosen_path = (path == NULL || path[0] == '\0') ? "." : path;
    AstraVfsResult result = astra_vfs_list(
        terminal->filesystem,
        chosen_path,
        text_length(chosen_path),
        entries,
        ASTRA_VFS_MAX_NODES,
        &count
    );
    if (result != ASTRA_VFS_OK) {
        terminal_write_result(terminal, result);
        return;
    }
    for (size_t i = 0u; i < count; i++) {
        terminal_write_text(terminal, entries[i].name);
        if (entries[i].is_directory) {
            terminal_put_byte(terminal, (uint8_t)'/');
        }
        terminal_put_byte(terminal, (uint8_t)'\n');
    }
    if (count == 0u) {
        terminal_write_text(terminal, "(empty)\n");
    }
}

static void terminal_execute(DesktopTerminal *terminal) {
    char command_line[DESKTOP_INPUT_CAP];
    size_t length = 0u;
    size_t command_end = 0u;
    size_t args_start = 0u;
    size_t args_end = 0u;
    const char *command = NULL;
    const char *args = NULL;
    size_t args_length = 0u;
    AstraVfsResult result = ASTRA_VFS_OK;

    if (terminal == NULL || terminal->filesystem == NULL) {
        return;
    }
    length = terminal->input_length;
    if (length >= sizeof command_line) {
        terminal_write_text(terminal, "error: command too long\n");
        return;
    }
    for (size_t i = 0u; i < length; i++) {
        command_line[i] = terminal->input[i];
    }
    command_line[length] = '\0';
    if (length == 0u) {
        terminal_newline(terminal);
        return;
    }

    command_end = 0u;
    while (command_end < length &&
        command_line[command_end] != ' ' && command_line[command_end] != '\t') {
        command_end++;
    }
    if (command_end == 0u) {
        terminal_newline(terminal);
        return;
    }
    command = command_line;
    args_start = skip_spaces(command_line, length, command_end);
    command_line[command_end] = '\0';
    args_end = length;
    while (args_end > args_start &&
        (command_line[args_end - 1u] == ' ' || command_line[args_end - 1u] == '\t')) {
        args_end--;
    }
    command_line[args_end] = '\0';
    args = &command_line[args_start];
    args_length = args_end - args_start;

    terminal_write_text(terminal, "> ");
    terminal_write(terminal, terminal->input, terminal->input_length);
    terminal_put_byte(terminal, (uint8_t)'\n');

    if (text_equals(command, "help")) {
        terminal_write_text(terminal,
            "help uname about pwd ls [PATH] cat PATH\n"
            "cd PATH touch PATH mkdir PATH rm PATH\n"
            "write PATH TEXT clear exit\n");
    } else if (text_equals(command, "uname")) {
        terminal_write_text(terminal, "AstraOS astra-aarch64 0.2 kernel\n");
    } else if (text_equals(command, "about")) {
        terminal_write_text(terminal,
            "AstraOS: freestanding AArch64 kernel.\n"
            "This window is a built-in client of the kernel compositor.\n"
            "The filesystem is in RAM and resets at reboot.\n");
    } else if (text_equals(command, "pwd")) {
        char path[ASTRA_VFS_PATH_CAP];
        size_t path_length = 0u;
        result = astra_vfs_pwd(terminal->filesystem, path, sizeof path, &path_length);
        if (result != ASTRA_VFS_OK) {
            terminal_write_result(terminal, result);
        } else {
            terminal_write(terminal, path, path_length);
            terminal_put_byte(terminal, (uint8_t)'\n');
        }
    } else if (text_equals(command, "ls")) {
        terminal_command_ls(terminal, args_length == 0u ? "." : args);
    } else if (text_equals(command, "cat")) {
        char content[ASTRA_VFS_CONTENT_CAP];
        size_t content_length = 0u;
        if (args_length == 0u) {
            terminal_write_text(terminal, "usage: cat PATH\n");
        } else {
            result = astra_vfs_read(
                terminal->filesystem, args, args_length,
                content, sizeof content, &content_length
            );
            if (result != ASTRA_VFS_OK) {
                terminal_write_result(terminal, result);
            } else {
                terminal_write(terminal, content, content_length);
                if (content_length == 0u ||
                    content[content_length - 1u] != '\n') {
                    terminal_put_byte(terminal, (uint8_t)'\n');
                }
            }
        }
    } else if (text_equals(command, "cd")) {
        if (args_length == 0u) {
            terminal_write_text(terminal, "usage: cd PATH\n");
        } else {
            result = astra_vfs_chdir(terminal->filesystem, args, args_length);
            if (result != ASTRA_VFS_OK) {
                terminal_write_result(terminal, result);
            }
        }
    } else if (text_equals(command, "touch") || text_equals(command, "mkdir")) {
        if (args_length == 0u) {
            terminal_write_text(terminal, text_equals(command, "touch")
                ? "usage: touch PATH\n" : "usage: mkdir PATH\n");
        } else {
            result = astra_vfs_create(
                terminal->filesystem, args, args_length,
                text_equals(command, "mkdir")
            );
            if (result != ASTRA_VFS_OK) {
                terminal_write_result(terminal, result);
            } else {
                terminal_write_text(terminal, "created\n");
            }
        }
    } else if (text_equals(command, "rm")) {
        if (args_length == 0u) {
            terminal_write_text(terminal, "usage: rm PATH\n");
        } else {
            result = astra_vfs_remove(terminal->filesystem, args, args_length);
            if (result != ASTRA_VFS_OK) {
                terminal_write_result(terminal, result);
            } else {
                terminal_write_text(terminal, "removed\n");
            }
        }
    } else if (text_equals(command, "write")) {
        size_t separator = 0u;
        size_t text_start = 0u;
        while (separator < args_length &&
            args[separator] != ' ' && args[separator] != '\t') {
            separator++;
        }
        if (separator == 0u || separator == args_length) {
            terminal_write_text(terminal, "usage: write PATH TEXT\n");
        } else {
            text_start = skip_spaces(args, args_length, separator);
            if (text_start == args_length) {
                terminal_write_text(terminal, "usage: write PATH TEXT\n");
            } else {
                result = astra_vfs_write(
                    terminal->filesystem, args, separator,
                    &args[text_start], args_length - text_start
                );
                if (result != ASTRA_VFS_OK) {
                    terminal_write_result(terminal, result);
                } else {
                    terminal_write_text(terminal, "written\n");
                }
            }
        }
    } else if (text_equals(command, "clear")) {
        terminal_reset(terminal);
    } else if (text_equals(command, "exit")) {
        terminal_write_text(terminal, "Use Escape to return to the kernel shell.\n");
    } else if (text_equals(command, "echo")) {
        terminal_write(terminal, args, args_length);
        terminal_put_byte(terminal, (uint8_t)'\n');
    } else {
        terminal_write_text(terminal, "unknown command; type help\n");
    }
}

static void paint_terminal(const AstraDisplayWindow *window, void *context) {
    DesktopTerminal *terminal = (DesktopTerminal *)context;
    size_t input_y = 0u;
    size_t history_slots = 0u;
    size_t first_to_draw = 0u;
    size_t columns = 0u;

    if (window == NULL || terminal == NULL) {
        return;
    }
    astra_display_window_fill(window, 0u, 0u, window->content_width,
        window->content_height, DESKTOP_COLOR_TERMINAL_BG);
    astra_display_window_text(window, DESKTOP_TEXT_X, DESKTOP_HEADER_Y,
        "Built-in shell  |  help for commands", 36u,
        DESKTOP_COLOR_ACCENT, DESKTOP_COLOR_TERMINAL_BG);

    columns = window->content_width > DESKTOP_TEXT_X
        ? (size_t)((window->content_width - DESKTOP_TEXT_X) / 8u) : 0u;
    input_y = window->content_height > DESKTOP_LINE_HEIGHT
        ? (size_t)window->content_height - DESKTOP_LINE_HEIGHT : 0u;
    if (input_y > DESKTOP_HISTORY_Y) {
        history_slots = (input_y - DESKTOP_HISTORY_Y) / DESKTOP_LINE_HEIGHT;
    }
    if (history_slots > terminal->line_count) {
        history_slots = terminal->line_count;
    }
    first_to_draw = terminal->line_count - history_slots;
    for (size_t line = 0u; line < history_slots; line++) {
        size_t index = (terminal->first_line + first_to_draw + line) %
            (size_t)DESKTOP_LOG_LINES;
        size_t line_length = terminal->line_lengths[index];
        if (line_length > columns) {
            line_length = columns;
        }
        astra_display_window_text(window, DESKTOP_TEXT_X,
            DESKTOP_HISTORY_Y + ((uint32_t)line * DESKTOP_LINE_HEIGHT),
            terminal->lines[index], line_length,
            DESKTOP_COLOR_TERMINAL_FG, DESKTOP_COLOR_TERMINAL_BG);
    }

    astra_display_window_text(window, DESKTOP_TEXT_X, (uint32_t)input_y,
        "> ", 2u, DESKTOP_COLOR_TEXT, DESKTOP_COLOR_TERMINAL_BG);
    if (columns > 2u && terminal->input_length > 0u) {
        size_t max_input = columns - 2u;
        size_t start = terminal->input_length > max_input
            ? terminal->input_length - max_input : 0u;
        astra_display_window_text(window, DESKTOP_TEXT_X + 16u,
            (uint32_t)input_y, &terminal->input[start],
            terminal->input_length - start,
            DESKTOP_COLOR_TEXT, DESKTOP_COLOR_TERMINAL_BG);
    }
    if (window->content_width >= DESKTOP_TEXT_X + 24u) {
        uint32_t cursor_x = DESKTOP_TEXT_X + 16u;
        size_t cursor_chars = terminal->input_length;
        if (columns > 2u && cursor_chars > columns - 2u) {
            cursor_chars = columns - 2u;
        }
        cursor_x += (uint32_t)cursor_chars * 8u;
        astra_display_window_fill(window, cursor_x, (uint32_t)input_y + 2u,
            2u, 12u, DESKTOP_COLOR_ACCENT);
    }
}

static void append_u32(char *output, size_t capacity, size_t *length, uint32_t value) {
    char reversed[10];
    size_t digits = 0u;

    if (output == NULL || length == NULL || capacity == 0u || *length >= capacity) {
        return;
    }
    do {
        reversed[digits] = (char)('0' + (value % 10u));
        value /= 10u;
        digits++;
    } while (value != 0u && digits < sizeof reversed);
    while (digits > 0u && *length + 1u < capacity) {
        digits--;
        output[*length] = reversed[digits];
        (*length)++;
    }
    output[*length] = '\0';
}

static void paint_system(const AstraDisplayWindow *window, void *context) {
    char resolution[32] = "Resolution: ";
    size_t resolution_length = text_length(resolution);
    (void)context;

    if (window == NULL) {
        return;
    }
    astra_display_window_text(window, 8u, 8u, "AstraOS desktop session", 22u,
        DESKTOP_COLOR_ACCENT, DESKTOP_COLOR_TERMINAL_BG);
    astra_display_window_text(window, 8u, 32u, "Display: UEFI GOP framebuffer", 29u,
        DESKTOP_COLOR_TEXT, DESKTOP_COLOR_TERMINAL_BG);
    astra_display_window_text(window, 8u, 56u, "Input: firmware keyboard / PL011", 32u,
        DESKTOP_COLOR_TEXT, DESKTOP_COLOR_TERMINAL_BG);
    astra_display_window_text(window, 8u, 80u, "Window slots: four fixed kernel records", 38u,
        DESKTOP_COLOR_TEXT, DESKTOP_COLOR_TERMINAL_BG);
    astra_display_window_text(window, 8u, 104u, "Processes and IPC are not implemented yet.", 41u,
        DESKTOP_COLOR_MUTED, DESKTOP_COLOR_TERMINAL_BG);

    append_u32(resolution, sizeof resolution, &resolution_length,
        astra_display_server_width());
    if (resolution_length + 1u < sizeof resolution) {
        resolution[resolution_length++] = 'x';
        resolution[resolution_length] = '\0';
    }
    append_u32(resolution, sizeof resolution, &resolution_length,
        astra_display_server_height());
    astra_display_window_text(window, 8u, 128u, resolution, resolution_length,
        DESKTOP_COLOR_TEXT, DESKTOP_COLOR_TERMINAL_BG);
    astra_display_window_text(window, 8u, 160u, "F1 Terminal  F2 System  F3 Files", 32u,
        DESKTOP_COLOR_MUTED, DESKTOP_COLOR_TERMINAL_BG);
}

static void paint_files(const AstraDisplayWindow *window, void *context) {
    AstraVfs *filesystem = (AstraVfs *)context;
    AstraVfsEntry entries[ASTRA_VFS_MAX_NODES];
    size_t count = 0u;
    AstraVfsResult result = ASTRA_VFS_OK;
    uint32_t y = 8u;

    if (window == NULL || filesystem == NULL) {
        return;
    }
    astra_display_window_text(window, 8u, 8u, "Temporary RAM filesystem /", 26u,
        DESKTOP_COLOR_ACCENT, DESKTOP_COLOR_TERMINAL_BG);
    result = astra_vfs_list(filesystem, "/", 1u, entries,
        ASTRA_VFS_MAX_NODES, &count);
    if (result != ASTRA_VFS_OK) {
        astra_display_window_text(window, 8u, 32u,
            astra_vfs_result_name(result), text_length(astra_vfs_result_name(result)),
            DESKTOP_COLOR_TEXT, DESKTOP_COLOR_TERMINAL_BG);
        return;
    }
    y = 32u;
    for (size_t i = 0u; i < count; i++) {
        uint32_t color = entries[i].is_directory
            ? DESKTOP_COLOR_DIRECTORY : DESKTOP_COLOR_FILE;
        astra_display_window_text(window, 8u, y, entries[i].name,
            text_length(entries[i].name), color, DESKTOP_COLOR_TERMINAL_BG);
        if (entries[i].is_directory) {
            uint32_t suffix_x = 8u +
                ((uint32_t)text_length(entries[i].name) * 8u);
            astra_display_window_text(window, suffix_x, y, "/", 1u, color,
                DESKTOP_COLOR_TERMINAL_BG);
        }
        if (window->content_height - y < DESKTOP_LINE_HEIGHT) {
            break;
        }
        y += DESKTOP_LINE_HEIGHT;
    }
    if (count == 0u) {
        astra_display_window_text(window, 8u, 32u, "(empty)", 7u,
            DESKTOP_COLOR_MUTED, DESKTOP_COLOR_TERMINAL_BG);
    }
}

static bool open_terminal(void) {
    bool result = astra_display_server_open_window(
        "Terminal",
        astra_display_server_width() > 720u
            ? DESKTOP_LARGE_WINDOW_WIDTH : DESKTOP_SMALL_WINDOW_WIDTH,
        astra_display_server_height() > 520u
            ? DESKTOP_LARGE_WINDOW_HEIGHT : DESKTOP_SMALL_WINDOW_HEIGHT,
        paint_terminal,
        &g_terminal,
        &g_terminal_window_id
    );
    if (result) {
        astra_uart_puts("AstraOS desktop: Terminal opened.\n");
    } else {
        astra_uart_puts("AstraOS desktop: could not open Terminal window.\n");
    }
    return result;
}

static bool open_system(void) {
    bool result = astra_display_server_open_window(
        "System monitor", 460u, 300u, paint_system, NULL, &g_system_window_id
    );
    if (result) {
        astra_uart_puts("AstraOS desktop: System monitor opened.\n");
    } else {
        astra_uart_puts("AstraOS desktop: could not open System monitor.\n");
    }
    return result;
}

static bool open_files(void) {
    bool result = astra_display_server_open_window(
        "Files", 420u, 320u, paint_files, g_terminal.filesystem, &g_files_window_id
    );
    if (result) {
        astra_uart_puts("AstraOS desktop: Files opened.\n");
    } else {
        astra_uart_puts("AstraOS desktop: could not open Files.\n");
    }
    return result;
}

static void handle_terminal_key(uint8_t key) {
    if (key == (uint8_t)'\r' || key == (uint8_t)'\n') {
        terminal_execute(&g_terminal);
        g_terminal.input_length = 0u;
        g_terminal.input[0] = '\0';
        return;
    }
    if (key == 0x08u || key == 0x7fu) {
        if (g_terminal.input_length > 0u) {
            g_terminal.input_length--;
            g_terminal.input[g_terminal.input_length] = '\0';
        }
        return;
    }
    if (key >= 0x20u && key <= 0x7eu &&
        g_terminal.input_length + 1u < sizeof g_terminal.input) {
        g_terminal.input[g_terminal.input_length] = (char)key;
        g_terminal.input_length++;
        g_terminal.input[g_terminal.input_length] = '\0';
    }
}

bool astra_desktop_run(AstraVfs *filesystem) {
    uint8_t key = 0u;
    bool running = true;

    if (filesystem == NULL) {
        astra_uart_puts("error: desktop requires an initialized filesystem.\n");
        return false;
    }
    if (!astra_display_server_initialize()) {
        astra_uart_puts(
            "error: desktop needs a supported GOP framebuffer at least 320x200.\n"
        );
        return false;
    }

    g_terminal.filesystem = filesystem;
    terminal_reset(&g_terminal);
    g_terminal.input_length = 0u;
    g_terminal.input[0] = '\0';
    g_terminal_window_id = 0u;
    g_system_window_id = 0u;
    g_files_window_id = 0u;
    terminal_write_text(&g_terminal, "AstraOS graphical terminal is ready.\n");
    terminal_write_text(&g_terminal, "Type help to use the temporary RAM filesystem.\n");
    terminal_write_text(&g_terminal, "F2 opens system info; F3 opens files.\n");

    if (!open_terminal()) {
        astra_display_server_shutdown();
        return false;
    }
    astra_uart_puts(
        "AstraOS desktop started. F1=Terminal F2=System F3=Files TAB=focus ESC=return.\n"
    );

    while (running) {
        const AstraDisplayWindow *focused = NULL;
        astra_display_server_render();
        if (!astra_input_read_byte(&key)) {
            astra_uart_puts("AstraOS desktop input stopped.\n");
            break;
        }
        if (key == 0x1bu) {
            running = false;
            continue;
        }
        if (key == (uint8_t)ASTRA_INPUT_KEY_F1) {
            (void)open_terminal();
            continue;
        }
        if (key == (uint8_t)ASTRA_INPUT_KEY_F2) {
            (void)open_system();
            continue;
        }
        if (key == (uint8_t)ASTRA_INPUT_KEY_F3) {
            (void)open_files();
            continue;
        }
        if (key == (uint8_t)'\t') {
            (void)astra_display_server_focus_next();
            continue;
        }
        focused = astra_display_server_focused_window();
        if (focused != NULL && focused->id == g_terminal_window_id) {
            handle_terminal_key(key);
        }
    }

    astra_display_server_shutdown();
    astra_uart_puts("Returned to AstraOS shell.\n");
    return true;
}
