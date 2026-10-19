#include "boot_info.h"
#include "acpi.h"
#include "framebuffer.h"
#include "fdt.h"
#include "exceptions.h"
#include "memory.h"
#include "input.h"
#include "uart.h"
#include "vfs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { KERNEL_LINE_CAP = 256 };

static AstraVfs g_vfs;
static AstraMemory g_memory;
static bool g_memory_ready = false;

static size_t text_length(const char *text) {
    size_t length = 0;

    if (text == NULL) {
        return 0;
    }
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static bool token_equals(const char *token, size_t token_len, const char *expected) {
    size_t expected_len = text_length(expected);

    if (token == NULL || token_len != expected_len) {
        return false;
    }
    for (size_t i = 0; i < token_len; i++) {
        if (token[i] != expected[i]) {
            return false;
        }
    }
    return true;
}

static const char *username(uint32_t uid) {
    if (uid == (uint32_t)ASTRA_VFS_ROOT_UID) {
        return "root";
    }
    if (uid == (uint32_t)ASTRA_VFS_ASTRA_UID) {
        return "astra";
    }
    return "unknown";
}

static void print_mode(uint16_t mode, bool is_directory) {
    char permissions[11] = {0};

    if (!astra_vfs_format_mode(mode, is_directory, permissions, sizeof permissions)) {
        astra_uart_puts("??????????");
        return;
    }
    astra_uart_write(permissions, sizeof permissions - 1u);
}

static void print_entry(const AstraVfsEntry *entry) {
    print_mode(entry->mode, entry->is_directory);
    astra_uart_puts("  ");
    astra_uart_puts(username(entry->owner_uid));
    astra_uart_puts("  ");
    astra_uart_puts(entry->name);
    if (entry->is_directory) {
        astra_uart_put_byte((uint8_t)'/');
    } else {
        astra_uart_puts("  ");
        astra_uart_put_u32((uint32_t)entry->size);
    }
    astra_uart_puts("\n");
}

static void print_result(AstraVfsResult result) {
    astra_uart_puts("error: ");
    astra_uart_puts(astra_vfs_result_name(result));
    astra_uart_puts("\n");
}

static size_t skip_spaces(const char *line, size_t length, size_t start) {
    while (start < length && (line[start] == ' ' || line[start] == '\t')) {
        start++;
    }
    return start;
}

static bool parse_command(
    char *line,
    size_t line_len,
    char **command_out,
    size_t *command_len_out,
    char **args_out,
    size_t *args_len_out
) {
    size_t command_start = 0;
    size_t command_end = 0;
    size_t args_start = 0;
    size_t args_end = line_len;

    if (line == NULL || command_out == NULL || command_len_out == NULL ||
        args_out == NULL || args_len_out == NULL) {
        return false;
    }

    command_start = skip_spaces(line, line_len, 0);
    if (command_start == line_len) {
        return false;
    }
    command_end = command_start;
    while (command_end < line_len && line[command_end] != ' ' &&
        line[command_end] != '\t') {
        command_end++;
    }

    *command_out = &line[command_start];
    *command_len_out = command_end - command_start;

    args_start = skip_spaces(line, line_len, command_end);
    line[command_end] = '\0';
    while (args_end > args_start &&
        (line[args_end - 1u] == ' ' || line[args_end - 1u] == '\t')) {
        args_end--;
    }
    line[args_end] = '\0';
    *args_out = &line[args_start];
    *args_len_out = args_end - args_start;
    return true;
}

static bool parse_hex_u64(const char *text, size_t text_len, uint64_t *value_out) {
    size_t position = 0;
    uint64_t value = 0;

    if (text == NULL || value_out == NULL || text_len == 0) {
        return false;
    }
    if (text_len >= 2u && text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')) {
        position = 2u;
    }
    if (position == text_len) {
        return false;
    }

    for (; position < text_len; position++) {
        uint64_t digit = 0;
        char character = text[position];

        if (character >= '0' && character <= '9') {
            digit = (uint64_t)(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = (uint64_t)(character - 'a') + 10u;
        } else if (character >= 'A' && character <= 'F') {
            digit = (uint64_t)(character - 'A') + 10u;
        } else {
            return false;
        }
        if (value > (UINT64_MAX - digit) / UINT64_C(16)) {
            return false;
        }
        value = (value * UINT64_C(16)) + digit;
    }

    *value_out = value;
    return true;
}

static bool parse_octal(
    const char *text,
    size_t text_len,
    uint16_t *mode_out
) {
    uint32_t value = 0;

    if (text == NULL || mode_out == NULL || text_len == 0) {
        return false;
    }
    for (size_t i = 0; i < text_len; i++) {
        uint32_t digit = 0;
        if (text[i] < '0' || text[i] > '7') {
            return false;
        }
        digit = (uint32_t)(text[i] - '0');
        if (value > (07777u - digit) / 8u) {
            return false;
        }
        value = (value * 8u) + digit;
    }
    *mode_out = (uint16_t)value;
    return true;
}

static void command_ls(const char *path, size_t path_len) {
    AstraVfsEntry entries[ASTRA_VFS_MAX_NODES];
    size_t count = 0;
    AstraVfsResult result = astra_vfs_list(
        &g_vfs, path, path_len, entries, ASTRA_VFS_MAX_NODES, &count
    );

    if (result != ASTRA_VFS_OK) {
        print_result(result);
        return;
    }
    astra_uart_puts("MODE        OWNER   NAME / SIZE\n");
    for (size_t i = 0; i < count; i++) {
        print_entry(&entries[i]);
    }
}

static void command_cat(const char *path, size_t path_len) {
    char content[ASTRA_VFS_CONTENT_CAP];
    size_t content_len = 0;
    AstraVfsResult result = astra_vfs_read(
        &g_vfs, path, path_len, content, sizeof content, &content_len
    );

    if (result != ASTRA_VFS_OK) {
        print_result(result);
        return;
    }
    astra_uart_write(content, content_len);
    if (content_len == 0 || content[content_len - 1u] != '\n') {
        astra_uart_puts("\n");
    }
}

static void command_write(const char *args, size_t args_len) {
    size_t separator = 0;
    size_t text_start = 0;
    AstraVfsResult result = ASTRA_VFS_OK;

    while (separator < args_len && args[separator] != ' ' && args[separator] != '\t') {
        separator++;
    }
    if (separator == 0 || separator == args_len) {
        astra_uart_puts("usage: write PATH TEXT\n");
        return;
    }

    text_start = skip_spaces(args, args_len, separator);
    result = astra_vfs_write(
        &g_vfs,
        args,
        separator,
        &args[text_start],
        args_len - text_start
    );
    if (result != ASTRA_VFS_OK) {
        print_result(result);
        return;
    }
    astra_uart_puts("written\n");
}

static void command_chmod(const char *args, size_t args_len) {
    size_t separator = 0;
    size_t path_start = 0;
    uint16_t mode = 0;
    AstraVfsResult result = ASTRA_VFS_OK;

    while (separator < args_len && args[separator] != ' ' && args[separator] != '\t') {
        separator++;
    }
    if (separator == 0 || separator == args_len ||
        !parse_octal(args, separator, &mode)) {
        astra_uart_puts("usage: chmod OCTAL PATH\n");
        return;
    }
    path_start = skip_spaces(args, args_len, separator);
    if (path_start == args_len) {
        astra_uart_puts("usage: chmod OCTAL PATH\n");
        return;
    }

    result = astra_vfs_chmod(&g_vfs, &args[path_start], args_len - path_start, mode);
    if (result != ASTRA_VFS_OK) {
        print_result(result);
        return;
    }
    astra_uart_puts("permissions updated\n");
}

static void print_device_tree_info(const AstraBootInfo *boot_info);

static void command_dispatch(const char *command, size_t command_len,
    const char *args, size_t args_len, const AstraBootInfo *boot_info) {
    AstraVfsResult result = ASTRA_VFS_OK;

    if (token_equals(command, command_len, "help")) {
        astra_uart_puts(
            "AstraOS kernel commands:\n"
            "  help, about, uname, platform, mem\n"
            "  palloc, pfree HEX_ADDRESS, clear, halt, exception-test\n"
            "  pwd, ls [PATH], cd PATH, cat PATH\n"
            "  touch PATH, mkdir PATH, write PATH TEXT, rm PATH\n"
            "  chmod OCTAL PATH, whoami, id, su astra\n"
            "Files live in RAM and reset at reboot.\n"
        );
    } else if (token_equals(command, command_len, "about")) {
        astra_uart_puts(
            "AstraOS kernel 0.2: freestanding AArch64 kernel.\n"
            "UEFI is used for boot, device discovery, and the memory-map handoff.\n"
            "The console uses a validated FDT or ACPI SPCR PL011 when available.\n"
            "Framebuffer-only platforms still need a native input driver.\n"
        );
    } else if (token_equals(command, command_len, "uname")) {
        astra_uart_puts("AstraOS astra-aarch64 0.2 kernel\n");
    } else if (token_equals(command, command_len, "platform")) {
        AstraAcpiSpcr spcr = {0};

        if (args_len != 0u) {
            astra_uart_puts("usage: platform\n");
            return;
        }
        astra_uart_puts("Platform diagnostics:\n");
        print_device_tree_info(boot_info);
        if (astra_acpi_find_pl011_uart(boot_info, &spcr)) {
            astra_uart_puts("ACPI SPCR PL011 base: ");
            astra_uart_put_hex(spcr.base_address);
            astra_uart_puts("\n");
        } else {
            astra_uart_puts("ACPI SPCR: no supported PL011 console advertised.\n");
        }
    } else if (token_equals(command, command_len, "mem")) {
        AstraMemoryStats stats = {0};
        AstraMemoryResult memory_result = ASTRA_MEMORY_OK;

        if (boot_info == NULL || boot_info->magic != ASTRA_BOOT_INFO_MAGIC ||
            boot_info->version != ASTRA_BOOT_INFO_VERSION) {
            astra_uart_puts("boot information unavailable\n");
            return;
        }
        astra_uart_puts("kernel base: ");
        astra_uart_put_hex(boot_info->kernel_base);
        astra_uart_puts("\nimage bytes: ");
        astra_uart_put_u64(boot_info->kernel_image_size);
        astra_uart_puts("\nUEFI map bytes: ");
        astra_uart_put_u64(boot_info->memory_map_size);
        astra_uart_puts("\ndescriptor bytes: ");
        astra_uart_put_u64(boot_info->memory_descriptor_size);
        astra_uart_puts("\n");

        if (!g_memory_ready) {
            astra_uart_puts("physical page allocator: unavailable\n");
            return;
        }
        memory_result = astra_memory_get_stats(&g_memory, &stats);
        if (memory_result != ASTRA_MEMORY_OK) {
            astra_uart_puts("error: ");
            astra_uart_puts(astra_memory_result_name(memory_result));
            astra_uart_puts("\n");
            return;
        }
        astra_uart_puts("memory descriptors: ");
        astra_uart_put_u64(stats.descriptor_count);
        astra_uart_puts("\nmanaged pages below 4 GiB: ");
        astra_uart_put_u64(stats.managed_pages);
        astra_uart_puts("\nfree pages: ");
        astra_uart_put_u64(stats.free_pages);
        astra_uart_puts("\nignored pages above 4 GiB: ");
        astra_uart_put_u64(stats.ignored_pages);
        astra_uart_puts("\n");
    } else if (token_equals(command, command_len, "palloc")) {
        uint64_t address = 0;
        AstraMemoryResult memory_result = ASTRA_MEMORY_OK;

        if (args_len != 0) {
            astra_uart_puts("usage: palloc\n");
            return;
        }
        if (!g_memory_ready) {
            astra_uart_puts("error: physical page allocator unavailable\n");
            return;
        }
        memory_result = astra_memory_alloc_page(&g_memory, &address);
        if (memory_result != ASTRA_MEMORY_OK) {
            astra_uart_puts("error: ");
            astra_uart_puts(astra_memory_result_name(memory_result));
            astra_uart_puts("\n");
            return;
        }
        astra_uart_puts("allocated physical page at ");
        astra_uart_put_hex(address);
        astra_uart_puts("\n");
    } else if (token_equals(command, command_len, "pfree")) {
        uint64_t address = 0;
        AstraMemoryResult memory_result = ASTRA_MEMORY_OK;

        if (!parse_hex_u64(args, args_len, &address)) {
            astra_uart_puts("usage: pfree HEX_ADDRESS (example: pfree 0x40001000)\n");
            return;
        }
        if (!g_memory_ready) {
            astra_uart_puts("error: physical page allocator unavailable\n");
            return;
        }
        memory_result = astra_memory_free_page(&g_memory, address);
        if (memory_result != ASTRA_MEMORY_OK) {
            astra_uart_puts("error: ");
            astra_uart_puts(astra_memory_result_name(memory_result));
            astra_uart_puts("\n");
            return;
        }
        astra_uart_puts("physical page returned to allocator\n");
    } else if (token_equals(command, command_len, "clear")) {
        astra_uart_puts("\x1b[2J\x1b[H");
    } else if (token_equals(command, command_len, "halt")) {
        astra_uart_puts("AstraOS halted. Reset the machine to reboot.\n");
        for (;;) {
            __asm__ volatile("wfe");
        }
    } else if (token_equals(command, command_len, "exception-test")) {
        if (args_len != 0u) {
            astra_uart_puts("usage: exception-test\n");
            return;
        }
        astra_uart_puts("Triggering a synchronous BRK exception.\n");
        __asm__ volatile("brk #0");
        for (;;) {
            __asm__ volatile("wfe");
        }
    } else if (token_equals(command, command_len, "pwd")) {
        char path[ASTRA_VFS_PATH_CAP];
        size_t written = 0;
        result = astra_vfs_pwd(&g_vfs, path, sizeof path, &written);
        if (result != ASTRA_VFS_OK) {
            print_result(result);
            return;
        }
        astra_uart_write(path, written);
        astra_uart_puts("\n");
    } else if (token_equals(command, command_len, "ls")) {
        command_ls(args_len == 0 ? "." : args, args_len == 0 ? 1u : args_len);
    } else if (token_equals(command, command_len, "cd")) {
        if (args_len == 0) {
            astra_uart_puts("usage: cd PATH\n");
            return;
        }
        result = astra_vfs_chdir(&g_vfs, args, args_len);
        if (result != ASTRA_VFS_OK) {
            print_result(result);
        }
    } else if (token_equals(command, command_len, "cat")) {
        if (args_len == 0) {
            astra_uart_puts("usage: cat PATH\n");
            return;
        }
        command_cat(args, args_len);
    } else if (token_equals(command, command_len, "touch")) {
        if (args_len == 0) {
            astra_uart_puts("usage: touch PATH\n");
            return;
        }
        result = astra_vfs_create(&g_vfs, args, args_len, false);
        if (result != ASTRA_VFS_OK) {
            print_result(result);
        } else {
            astra_uart_puts("file created\n");
        }
    } else if (token_equals(command, command_len, "mkdir")) {
        if (args_len == 0) {
            astra_uart_puts("usage: mkdir PATH\n");
            return;
        }
        result = astra_vfs_create(&g_vfs, args, args_len, true);
        if (result != ASTRA_VFS_OK) {
            print_result(result);
        } else {
            astra_uart_puts("directory created\n");
        }
    } else if (token_equals(command, command_len, "write")) {
        command_write(args, args_len);
    } else if (token_equals(command, command_len, "rm")) {
        if (args_len == 0) {
            astra_uart_puts("usage: rm PATH\n");
            return;
        }
        result = astra_vfs_remove(&g_vfs, args, args_len);
        if (result != ASTRA_VFS_OK) {
            print_result(result);
        } else {
            astra_uart_puts("removed\n");
        }
    } else if (token_equals(command, command_len, "chmod")) {
        command_chmod(args, args_len);
    } else if (token_equals(command, command_len, "whoami")) {
        uint32_t uid = 0;
        result = astra_vfs_get_uid(&g_vfs, &uid);
        if (result != ASTRA_VFS_OK) {
            print_result(result);
            return;
        }
        astra_uart_puts(username(uid));
        astra_uart_puts("\n");
    } else if (token_equals(command, command_len, "id")) {
        uint32_t uid = 0;
        result = astra_vfs_get_uid(&g_vfs, &uid);
        if (result != ASTRA_VFS_OK) {
            print_result(result);
            return;
        }
        astra_uart_puts("uid=");
        astra_uart_put_u32(uid);
        astra_uart_puts("(");
        astra_uart_puts(username(uid));
        astra_uart_puts(") gid=");
        astra_uart_put_u32(uid);
        astra_uart_puts("(");
        astra_uart_puts(username(uid));
        astra_uart_puts(")\n");
    } else if (token_equals(command, command_len, "su")) {
        if (token_equals(args, args_len, "astra")) {
            result = astra_vfs_set_uid(&g_vfs, (uint32_t)ASTRA_VFS_ASTRA_UID);
            if (result != ASTRA_VFS_OK) {
                print_result(result);
            } else {
                astra_uart_puts("switched to unprivileged user astra\n");
            }
        } else {
            astra_uart_puts("authentication is not implemented; use 'su astra'\n");
        }
    } else if (token_equals(command, command_len, "echo")) {
        astra_uart_write(args, args_len);
        astra_uart_puts("\n");
    } else if (token_equals(command, command_len, "exit")) {
        astra_uart_puts("The kernel cannot return to UEFI. Use 'halt' to stop.\n");
    } else {
        astra_uart_puts("unknown command; type help\n");
    }
}

enum { DEVICE_TREE_PROPERTY_DISPLAY_CAP = 128u };

static void print_device_tree_info(const AstraBootInfo *boot_info) {
    AstraFdtView view;
    AstraFdtResult result = ASTRA_FDT_OK;
    const uint8_t *model = NULL;
    size_t model_length = 0;
    const uint8_t *compatible = NULL;
    size_t compatible_length = 0;

    if (boot_info == NULL || boot_info->device_tree_base == 0u ||
        boot_info->device_tree_size == 0u ||
        boot_info->device_tree_size > (uint64_t)ASTRA_FDT_MAX_SIZE ||
        boot_info->device_tree_base > (uint64_t)UINTPTR_MAX) {
        astra_uart_puts("Firmware device tree unavailable; board discovery is limited.\n");
        return;
    }

    result = astra_fdt_open(
        (const void *)(uintptr_t)boot_info->device_tree_base,
        (size_t)boot_info->device_tree_size,
        &view
    );
    if (result != ASTRA_FDT_OK) {
        astra_uart_puts("Warning: kernel rejected the firmware device tree: ");
        astra_uart_puts(astra_fdt_result_name(result));
        astra_uart_puts("\n");
        return;
    }

    astra_uart_puts("Firmware device tree validated.\n");
    result = astra_fdt_root_property(&view, "model", 5u, &model, &model_length);
    if (result == ASTRA_FDT_OK) {
        astra_uart_puts("Device model: ");
        for (size_t i = 0; i < model_length &&
            i < (size_t)DEVICE_TREE_PROPERTY_DISPLAY_CAP; i++) {
            uint8_t byte = model[i];

            if (byte == 0u) {
                break;
            }
            if (byte >= (uint8_t)' ' && byte <= (uint8_t)'~') {
                astra_uart_put_byte(byte);
            } else {
                astra_uart_put_byte((uint8_t)'?');
            }
        }
        astra_uart_puts("\n");
    } else if (result != ASTRA_FDT_PROPERTY_NOT_FOUND) {
        astra_uart_puts("Warning: could not read device-tree model: ");
        astra_uart_puts(astra_fdt_result_name(result));
        astra_uart_puts("\n");
    }


    {
        const uint8_t *stdout_path = NULL;
        size_t stdout_path_length = 0u;

        result = astra_fdt_node_property(
            &view,
            "/chosen",
            sizeof "/chosen" - 1u,
            "stdout-path",
            sizeof "stdout-path" - 1u,
            &stdout_path,
            &stdout_path_length
        );
        if (result == ASTRA_FDT_OK) {
            astra_uart_puts("FDT stdout-path: ");
            for (size_t i = 0u; i < stdout_path_length &&
                i < (size_t)DEVICE_TREE_PROPERTY_DISPLAY_CAP; i++) {
                uint8_t byte = stdout_path[i];

                if (byte == 0u) {
                    break;
                }
                if (byte >= (uint8_t)' ' && byte <= (uint8_t)'~') {
                    astra_uart_put_byte(byte);
                } else {
                    astra_uart_put_byte((uint8_t)'?');
                }
            }
            astra_uart_puts("\n");
        } else if (result == ASTRA_FDT_PROPERTY_NOT_FOUND) {
            astra_uart_puts("FDT stdout-path: not provided\n");
        } else {
            astra_uart_puts("Warning: could not read FDT stdout-path: ");
            astra_uart_puts(astra_fdt_result_name(result));
            astra_uart_puts("\n");
        }
    }

    result = astra_fdt_root_property(&view, "compatible", 10u,
        &compatible, &compatible_length);
    if (result == ASTRA_FDT_OK) {
        size_t display_length = compatible_length;
        astra_uart_puts("Device compatible: ");
        if (display_length > (size_t)DEVICE_TREE_PROPERTY_DISPLAY_CAP) {
            display_length = (size_t)DEVICE_TREE_PROPERTY_DISPLAY_CAP;
        }
        for (size_t i = 0; i < display_length; i++) {
            uint8_t byte = compatible[i];

            if (byte == 0u) {
                if (i + 1u < compatible_length && i + 1u < display_length) {
                    astra_uart_puts(", ");
                }
            } else if (byte >= (uint8_t)' ' && byte <= (uint8_t)'~') {
                astra_uart_put_byte(byte);
            } else {
                astra_uart_put_byte((uint8_t)'?');
            }
        }
        if (compatible_length > display_length) {
            astra_uart_puts("...");
        }
        astra_uart_puts("\n");
    } else if (result == ASTRA_FDT_PROPERTY_NOT_FOUND) {
        astra_uart_puts("Device compatible: not provided\n");
    } else {
        astra_uart_puts("Warning: could not read device-tree compatible: ");
        astra_uart_puts(astra_fdt_result_name(result));
        astra_uart_puts("\n");
    }
}

void astra_kernel_main(const AstraBootInfo *boot_info) {
    char line[KERNEL_LINE_CAP];
    AstraVfsResult result = ASTRA_VFS_OK;
    bool serial_ready = false;
    bool input_ready = false;

    astra_framebuffer_init(boot_info);
    serial_ready = astra_uart_init(boot_info);
    input_ready = astra_input_initialize(boot_info, serial_ready);
    if (!input_ready) {
        astra_uart_puts("\nAstraOS kernel 0.2.0 | AArch64 freestanding\n");
        astra_uart_puts(
            "No supported PL011 UART or UEFI keyboard input was found.\n"
        );
        astra_uart_puts(
            "Framebuffer diagnostics are display-only until an input source is available.\n"
        );
        return;
    }

    if (astra_framebuffer_is_active()) {
        astra_uart_puts("GOP framebuffer console active.\n");
    } else {
        astra_uart_puts("GOP framebuffer console unavailable.\n");
    }
    if (!serial_ready) {
        astra_uart_puts(
            "PL011 serial console unavailable; UEFI firmware keyboard input is active.\n"
        );
    }
    astra_uart_puts("\n================================\n");
    astra_uart_puts("          ASTRAOS KERNEL\n");
    astra_uart_puts("================================\n");
    astra_uart_puts("AstraOS kernel 0.2.0 | AArch64 freestanding\n");
    if (boot_info != NULL && boot_info->magic == ASTRA_BOOT_INFO_MAGIC &&
        boot_info->version == ASTRA_BOOT_INFO_VERSION) {
        AstraMemoryResult memory_result = ASTRA_MEMORY_OK;

        print_device_tree_info(boot_info);
        if ((boot_info->flags & ASTRA_BOOT_FLAG_BOOT_SERVICES_ACTIVE) != 0u) {
            g_memory_ready = false;
            astra_uart_puts(
                "UEFI boot services remain active for firmware keyboard input.\n"
            );
            astra_uart_puts(
                "Physical page allocation is disabled while firmware owns conventional RAM.\n"
            );
        } else {
            astra_uart_puts("UEFI boot-services handoff completed.\n");
            astra_uart_puts("Kernel base: ");
            astra_uart_put_hex(boot_info->kernel_base);
            astra_uart_puts("\n");

            memory_result = astra_memory_init(&g_memory, boot_info);
            if (memory_result == ASTRA_MEMORY_OK) {
                g_memory_ready = true;
                astra_uart_puts("Physical page allocator initialized (below 4 GiB).\n");
            } else {
                g_memory_ready = false;
                astra_uart_puts("warning: physical page allocator unavailable: ");
                astra_uart_puts(astra_memory_result_name(memory_result));
                astra_uart_puts("\n");
            }
        }
    } else {
        g_memory_ready = false;
        astra_uart_puts("warning: boot information did not validate\n");
    }

    result = astra_vfs_init(&g_vfs);
    if (result != ASTRA_VFS_OK) {
        print_result(result);
        for (;;) {
            __asm__ volatile("wfe");
        }
    }

    line[0] = '\0';
    for (;;) {
        char *command = NULL;
        char *args = NULL;
        size_t command_len = 0;
        size_t args_len = 0;
        size_t line_len = 0;
        uint32_t uid = 0;
        char path[ASTRA_VFS_PATH_CAP];
        size_t path_len = 0;

        if (astra_vfs_get_uid(&g_vfs, &uid) != ASTRA_VFS_OK) {
            uid = (uint32_t)ASTRA_VFS_ROOT_UID;
        }
        astra_uart_puts(username(uid));
        astra_uart_puts("@astra:");
        if (astra_vfs_pwd(&g_vfs, path, sizeof path, &path_len) == ASTRA_VFS_OK) {
            astra_uart_write(path, path_len);
        } else {
            astra_uart_puts("?");
        }
        astra_uart_puts(uid == (uint32_t)ASTRA_VFS_ROOT_UID ? "# " : "$ ");

        line_len = 0;
        line[0] = '\0';
        {
            bool line_too_long = false;

            for (;;) {
                uint8_t input = 0u;
                if (!astra_input_read_byte(&input)) {
                    astra_uart_puts("\nerror: console input stopped; halting safely.\n");
                    for (;;) {
                        __asm__ volatile("wfe");
                    }
                }
                if (input == (uint8_t)'\r' || input == (uint8_t)'\n') {
                    astra_uart_puts("\n");
                    break;
                }
                if (input == 0x08u || input == 0x7fu) {
                    if (line_len > 0) {
                        line_len--;
                        line[line_len] = '\0';
                        astra_uart_puts("\b \b");
                    }
                    continue;
                }
                if (input >= 0x20u && input < 0x7fu) {
                    if (line_len + 1u < sizeof line) {
                        line[line_len] = (char)input;
                        line_len++;
                        line[line_len] = '\0';
                        astra_uart_put_byte(input);
                    } else {
                        line_too_long = true;
                    }
                }
            }

            if (line_too_long) {
                astra_uart_puts("error: command too long; discarded\n");
                continue;
            }
        }

        if (parse_command(line, line_len, &command, &command_len, &args, &args_len)) {
            command_dispatch(command, command_len, args, args_len, boot_info);
        }
    }
}
