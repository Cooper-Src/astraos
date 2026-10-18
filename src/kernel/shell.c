#include "shell.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "astra/vfs.h"
#include "console.h"

enum {
    SHELL_LINE_CAPACITY = 256,
    SHELL_READ_CAPACITY = VFS_CONTENT_MAX + 1
};

static bool string_equal(const char *left, const char *right) {
    size_t i = 0;

    if (left == NULL || right == NULL) {
        return false;
    }
    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i]) {
            return false;
        }
        i++;
    }
    return left[i] == right[i];
}

static bool string_empty(const char *text) {
    return text == NULL || text[0] == '\0';
}

static char *skip_spaces(char *text) {
    while (text != NULL && (*text == ' ' || *text == '\t')) {
        text++;
    }
    return text;
}

static char *take_token(char **text) {
    char *start = skip_spaces(*text);
    char *end = start;

    if (start == NULL || *start == '\0') {
        *text = start;
        return start;
    }
    while (*end != '\0' && *end != ' ' && *end != '\t') {
        end++;
    }
    if (*end != '\0') {
        *end = '\0';
        end++;
    }
    *text = skip_spaces(end);
    return start;
}

static void print_status(VfsStatus status) {
    if (status != VFS_OK) {
        console_write("error: ");
        console_write(vfs_status_string(status));
        console_write("\n");
    }
}

static void print_mode(uint16_t mode, bool directory) {
    console_write_char(directory ? 'd' : '-');
    console_write_char((mode & 0400u) != 0u ? 'r' : '-');
    console_write_char((mode & 0200u) != 0u ? 'w' : '-');
    console_write_char((mode & 0100u) != 0u ? 'x' : '-');
    console_write_char((mode & 0040u) != 0u ? 'r' : '-');
    console_write_char((mode & 0020u) != 0u ? 'w' : '-');
    console_write_char((mode & 0010u) != 0u ? 'x' : '-');
    console_write_char((mode & 0004u) != 0u ? 'r' : '-');
    console_write_char((mode & 0002u) != 0u ? 'w' : '-');
    console_write_char((mode & 0001u) != 0u ? 'x' : '-');
}

static void command_ls(const char *path) {
    VfsEntry entries[VFS_MAX_NODES] = {0};
    size_t count = 0;
    VfsStatus status = vfs_list(path, entries, VFS_MAX_NODES, &count);

    if (status != VFS_OK) {
        print_status(status);
        return;
    }
    console_write("MODE         OWNER   SIZE   NAME\n");
    for (size_t i = 0; i < count; i++) {
        print_mode(entries[i].mode, entries[i].directory);
        console_write("  ");
        console_write(vfs_username(entries[i].owner));
        console_write("   ");
        console_write_u64((uint64_t)entries[i].size);
        console_write("   ");
        console_write(entries[i].name);
        if (entries[i].directory) {
            console_write_char('/');
        }
        console_write("\n");
    }
}

static void command_cat(const char *path) {
    char contents[SHELL_READ_CAPACITY] = {0};
    size_t length = 0;
    VfsStatus status = vfs_read(path, contents, sizeof contents, &length);

    if (status != VFS_OK) {
        print_status(status);
        return;
    }
    (void)length;
    console_write(contents);
    if (contents[0] != '\0') {
        size_t end = 0;
        while (contents[end] != '\0') {
            end++;
        }
        if (contents[end - 1u] != '\n') {
            console_write("\n");
        }
    }
}

static bool parse_octal_mode(const char *text, uint16_t *out_mode) {
    unsigned int value = 0;
    size_t position = 0;

    if (text == NULL || out_mode == NULL || text[0] == '\0') {
        return false;
    }
    while (text[position] != '\0') {
        unsigned int digit = 0;

        if (text[position] < '0' || text[position] > '7') {
            return false;
        }
        digit = (unsigned int)(text[position] - '0');
        if (value > (07777u - digit) / 8u) {
            return false;
        }
        value = value * 8u + digit;
        position++;
    }
    *out_mode = (uint16_t)value;
    return true;
}

static void command_chmod(char *arguments) {
    char *mode_text = take_token(&arguments);
    char *path = skip_spaces(arguments);
    uint16_t mode = 0;

    if (mode_text == NULL || string_empty(mode_text) || string_empty(path) ||
        !parse_octal_mode(mode_text, &mode)) {
        console_write("usage: chmod OCTAL PATH\n");
        return;
    }
    print_status(vfs_chmod(path, mode));
}

static void command_write(char *arguments) {
    char *path = take_token(&arguments);
    char *contents = skip_spaces(arguments);

    if (string_empty(path) || string_empty(contents)) {
        console_write("usage: write PATH TEXT\n");
        return;
    }
    print_status(vfs_write(path, contents));
}

static void command_su(const char *user) {
    if (string_equal(user, "astra")) {
        print_status(vfs_switch_user(VFS_UID_ASTRA));
        return;
    }
    if (string_equal(user, "root")) {
        if (vfs_get_uid() == VFS_UID_ROOT) {
            console_write("already root\n");
        } else {
            console_write("error: authentication is not implemented; restart to reset the demo identity\n");
        }
        return;
    }
    console_write("usage: su astra\n");
}

static bool dispatch_command(char *line) {
    char *arguments = skip_spaces(line);
    char *command = take_token(&arguments);
    VfsStatus status = VFS_OK;

    if (string_empty(command)) {
        return true;
    }
    if (string_equal(command, "help")) {
        console_write(
            "AstraOS ARM64 kernel prototype commands:\n"
            "  help, about, uname, clear, exit\n"
            "  pwd, ls [PATH], cd PATH, cat PATH\n"
            "  touch PATH, mkdir PATH, write PATH TEXT, rm PATH\n"
            "  chmod OCTAL PATH, whoami, id, su astra, echo TEXT\n"
            "Filesystem is RAM-only; demo permissions are not process isolation.\n");
    } else if (string_equal(command, "about")) {
        console_write(
            "AstraOS alpha: freestanding AArch64 kernel loaded through Limine.\n"
            "Kernel-owned framebuffer/serial console and bounded RAM VFS.\n"
            "There is no scheduler, user-mode isolation, persistent disk driver, or USB HID driver yet.\n");
    } else if (string_equal(command, "uname")) {
        console_write("AstraOS astra-aarch64 0.2.0 kernel\n");
    } else if (string_equal(command, "clear")) {
        console_clear();
    } else if (string_equal(command, "exit")) {
        console_write("Shell stopped. The kernel will remain halted until reset.\n");
        return false;
    } else if (string_equal(command, "pwd")) {
        char path[VFS_PATH_MAX + 1] = {0};
        status = vfs_getcwd(path, sizeof path);
        if (status == VFS_OK) {
            console_write(path);
            console_write("\n");
        }
    } else if (string_equal(command, "ls")) {
        command_ls(string_empty(arguments) ? "." : arguments);
    } else if (string_equal(command, "cd")) {
        if (string_empty(arguments)) {
            console_write("usage: cd PATH\n");
            return true;
        }
        status = vfs_chdir(arguments);
    } else if (string_equal(command, "cat")) {
        if (string_empty(arguments)) {
            console_write("usage: cat PATH\n");
            return true;
        }
        command_cat(arguments);
    } else if (string_equal(command, "touch")) {
        if (string_empty(arguments)) {
            console_write("usage: touch PATH\n");
            return true;
        }
        status = vfs_create(arguments, false);
    } else if (string_equal(command, "mkdir")) {
        if (string_empty(arguments)) {
            console_write("usage: mkdir PATH\n");
            return true;
        }
        status = vfs_create(arguments, true);
    } else if (string_equal(command, "write")) {
        command_write(arguments);
    } else if (string_equal(command, "rm")) {
        if (string_empty(arguments)) {
            console_write("usage: rm PATH\n");
            return true;
        }
        status = vfs_remove(arguments);
    } else if (string_equal(command, "chmod")) {
        command_chmod(arguments);
    } else if (string_equal(command, "whoami")) {
        console_write(vfs_username(vfs_get_uid()));
        console_write("\n");
    } else if (string_equal(command, "id")) {
        console_write("uid=");
        console_write_u64(vfs_get_uid());
        console_write("(");
        console_write(vfs_username(vfs_get_uid()));
        console_write(") gid=");
        console_write_u64(vfs_get_uid());
        console_write("(");
        console_write(vfs_username(vfs_get_uid()));
        console_write(")\n");
    } else if (string_equal(command, "su")) {
        command_su(arguments);
    } else if (string_equal(command, "echo")) {
        console_write(arguments);
        console_write("\n");
    } else {
        console_write("error: unknown command; type help\n");
    }
    print_status(status);
    return true;
}

void shell_run(void) {
    char line[SHELL_LINE_CAPACITY] = {0};

    for (;;) {
        char cwd[VFS_PATH_MAX + 1] = {0};
        size_t length = 0;
        VfsStatus cwd_status = vfs_getcwd(cwd, sizeof cwd);

        console_write(vfs_username(vfs_get_uid()));
        console_write("@astra:");
        console_write(cwd_status == VFS_OK ? cwd : "?");
        console_write("# ");

        for (;;) {
            int input = console_read_char();

            if (input < 0) {
                __asm__ volatile("wfe");
                continue;
            }
            if (input == '\r' || input == '\n') {
                console_write("\n");
                line[length] = '\0';
                if (!dispatch_command(line)) {
                    return;
                }
                break;
            }
            if (input == 8 || input == 127) {
                if (length > 0u) {
                    length--;
                    line[length] = '\0';
                    console_write_char('\b');
                }
                continue;
            }
            if (input >= 32 && input <= 126 && length + 1u < sizeof line) {
                line[length++] = (char)input;
                line[length] = '\0';
                console_write_char((char)input);
            }
        }
    }
}
