#include "platform.h"

#include "acpi.h"
#include "fdt.h"

#include <stddef.h>
#include <stdint.h>

enum {
    PLATFORM_PATH_CAP = 256u,
    PLATFORM_MAX_CELLS = 2u,
    PLATFORM_DEFAULT_ADDRESS_CELLS = 2u,
    PLATFORM_DEFAULT_SIZE_CELLS = 1u,
    PLATFORM_PL011_REGISTER_BYTES = 72u
};

static uint32_t platform_read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24u)
        | ((uint32_t)bytes[1] << 16u)
        | ((uint32_t)bytes[2] << 8u)
        | (uint32_t)bytes[3];
}

static bool platform_string_length(
    const uint8_t *bytes,
    size_t length,
    size_t *string_length_out
) {
    if (bytes == NULL || string_length_out == NULL) {
        return false;
    }
    for (size_t i = 0u; i < length; i++) {
        if (bytes[i] == 0u) {
            if (i == 0u) {
                return false;
            }
            *string_length_out = i;
            return true;
        }
    }
    return false;
}

static bool platform_copy_absolute_path(
    const uint8_t *bytes,
    size_t length,
    char *output,
    size_t output_capacity,
    size_t *output_length
) {
    if (bytes == NULL || output == NULL || output_length == NULL ||
        length == 0u || length >= output_capacity || bytes[0] != (uint8_t)'/') {
        return false;
    }
    for (size_t i = 0u; i < length; i++) {
        if (bytes[i] < (uint8_t)' ' || bytes[i] > (uint8_t)'~') {
            return false;
        }
        output[i] = (char)bytes[i];
    }
    output[length] = '\0';
    *output_length = length;
    return true;
}

/* Handles both /chosen/stdout-path="/path/to/uart" and alias:options forms. */
static bool platform_stdout_node_path(
    const AstraFdtView *view,
    char *path_out,
    size_t path_capacity,
    size_t *path_length_out
) {
    const uint8_t *stdout_value = NULL;
    const uint8_t *alias_value = NULL;
    size_t stdout_value_length = 0u;
    size_t stdout_length = 0u;
    size_t path_token_length = 0u;
    size_t alias_value_length = 0u;
    size_t alias_path_length = 0u;
    char alias_name[PLATFORM_PATH_CAP] = {0};
    AstraFdtResult result = ASTRA_FDT_OK;

    result = astra_fdt_node_property(
        view, "/chosen", sizeof "/chosen" - 1u,
        "stdout-path", sizeof "stdout-path" - 1u,
        &stdout_value, &stdout_value_length
    );
    if (result != ASTRA_FDT_OK ||
        !platform_string_length(stdout_value, stdout_value_length, &stdout_length)) {
        return false;
    }

    path_token_length = stdout_length;
    for (size_t i = 0u; i < stdout_length; i++) {
        if (stdout_value[i] == (uint8_t)':') {
            path_token_length = i;
            break;
        }
    }
    if (path_token_length == 0u) {
        return false;
    }
    if (stdout_value[0] == (uint8_t)'/') {
        return platform_copy_absolute_path(
            stdout_value, path_token_length,
            path_out, path_capacity, path_length_out
        );
    }
    if (path_token_length >= sizeof alias_name) {
        return false;
    }
    for (size_t i = 0u; i < path_token_length; i++) {
        const uint8_t character = stdout_value[i];
        if (character == (uint8_t)'/' ||
            character < (uint8_t)'!' || character > (uint8_t)'~') {
            return false;
        }
        alias_name[i] = (char)character;
    }
    alias_name[path_token_length] = '\0';

    result = astra_fdt_node_property(
        view, "/aliases", sizeof "/aliases" - 1u,
        alias_name, path_token_length,
        &alias_value, &alias_value_length
    );
    if (result != ASTRA_FDT_OK ||
        !platform_string_length(alias_value, alias_value_length, &alias_path_length)) {
        return false;
    }
    return platform_copy_absolute_path(
        alias_value, alias_path_length,
        path_out, path_capacity, path_length_out
    );
}

static bool platform_parent_path(
    const char *node_path,
    size_t node_path_length,
    char *parent_out,
    size_t parent_capacity,
    size_t *parent_length_out
) {
    size_t last_slash = 0u;

    if (node_path == NULL || parent_out == NULL || parent_length_out == NULL ||
        node_path_length < 2u || node_path_length >= parent_capacity ||
        node_path[0] != '/' || node_path[node_path_length - 1u] == '/') {
        return false;
    }
    for (size_t i = 1u; i < node_path_length; i++) {
        if (node_path[i] == '/') {
            last_slash = i;
        }
    }
    if (last_slash == 0u) {
        parent_out[0] = '/';
        parent_out[1] = '\0';
        *parent_length_out = 1u;
        return true;
    }
    for (size_t i = 0u; i < last_slash; i++) {
        parent_out[i] = node_path[i];
    }
    parent_out[last_slash] = '\0';
    *parent_length_out = last_slash;
    return true;
}

static bool platform_cell_count(
    const AstraFdtView *view,
    const char *parent_path,
    size_t parent_path_length,
    const char *property_name,
    size_t property_name_length,
    uint32_t default_value,
    uint32_t *value_out
) {
    const uint8_t *bytes = NULL;
    size_t length = 0u;
    AstraFdtResult result = ASTRA_FDT_OK;
    uint32_t value = 0u;

    if (value_out == NULL) {
        return false;
    }
    result = astra_fdt_node_property(
        view, parent_path, parent_path_length,
        property_name, property_name_length, &bytes, &length
    );
    if (result == ASTRA_FDT_PROPERTY_NOT_FOUND) {
        *value_out = default_value;
        return true;
    }
    if (result != ASTRA_FDT_OK || length != 4u) {
        return false;
    }
    value = platform_read_be32(bytes);
    if (value == 0u || value > (uint32_t)PLATFORM_MAX_CELLS) {
        return false;
    }
    *value_out = value;
    return true;
}

static bool platform_read_cells(
    const uint8_t *bytes,
    size_t length,
    uint32_t cell_count,
    uint64_t *value_out
) {
    uint64_t value = 0u;

    if (bytes == NULL || value_out == NULL || cell_count == 0u ||
        cell_count > (uint32_t)PLATFORM_MAX_CELLS ||
        length < ((size_t)cell_count * 4u)) {
        return false;
    }
    for (size_t i = 0u; i < (size_t)cell_count; i++) {
        if (value > (UINT64_MAX >> 32u)) {
            return false;
        }
        value = (value << 32u) | (uint64_t)platform_read_be32(&bytes[i * 4u]);
    }
    *value_out = value;
    return true;
}

static bool platform_string_list_contains(
    const uint8_t *bytes,
    size_t length,
    const char *target,
    size_t target_length
) {
    size_t position = 0u;
    bool found = false;

    if (bytes == NULL || target == NULL || target_length == 0u) {
        return false;
    }
    while (position < length) {
        size_t end = position;
        bool equal = true;

        while (end < length && bytes[end] != 0u) {
            end++;
        }
        if (end == position || end == length) {
            return false;
        }
        if (end - position == target_length) {
            for (size_t i = 0u; i < target_length; i++) {
                if (bytes[position + i] != (uint8_t)target[i]) {
                    equal = false;
                    break;
                }
            }
            if (equal) {
                found = true;
            }
        }
        position = end + 1u;
    }
    return found;
}

bool astra_platform_find_pl011_uart_from_fdt(
    const AstraBootInfo *boot_info,
    uint64_t *base_out
) {
    AstraFdtView view = {0};
    AstraFdtResult result = ASTRA_FDT_OK;
    char node_path[PLATFORM_PATH_CAP] = {0};
    char parent_path[PLATFORM_PATH_CAP] = {0};
    size_t node_path_length = 0u;
    size_t parent_path_length = 0u;
    const uint8_t *compatible = NULL;
    size_t compatible_length = 0u;
    const uint8_t *reg = NULL;
    size_t reg_length = 0u;
    uint32_t address_cells = 0u;
    uint32_t size_cells = 0u;
    size_t tuple_length = 0u;
    uint64_t base = 0u;
    uint64_t register_size = 0u;
    uint64_t dtb_base = 0u;
    uint64_t dtb_size = 0u;

    if (boot_info == NULL || base_out == NULL ||
        boot_info->magic != ASTRA_BOOT_INFO_MAGIC ||
        boot_info->version != ASTRA_BOOT_INFO_VERSION) {
        return false;
    }

    dtb_base = boot_info->device_tree_base;
    dtb_size = boot_info->device_tree_size;
    if (dtb_base == 0u || dtb_base > (uint64_t)UINTPTR_MAX ||
        dtb_size == 0u || dtb_size > (uint64_t)ASTRA_FDT_MAX_SIZE ||
        dtb_size > (uint64_t)SIZE_MAX ||
        dtb_size > (uint64_t)UINTPTR_MAX - dtb_base) {
        return false;
    }

    result = astra_fdt_open((const void *)(uintptr_t)dtb_base, (size_t)dtb_size, &view);
    if (result != ASTRA_FDT_OK ||
        !platform_stdout_node_path(&view, node_path, sizeof node_path, &node_path_length) ||
        !platform_parent_path(
            node_path, node_path_length,
            parent_path, sizeof parent_path, &parent_path_length
        )) {
        return false;
    }

    /* Nested buses require translating reg through ancestor ranges.
     * Until that is implemented, accept only root-relative MMIO addresses. */
    if (parent_path_length != 1u) {
        return false;
    }

    result = astra_fdt_node_property(
        &view, node_path, node_path_length,
        "compatible", sizeof "compatible" - 1u,
        &compatible, &compatible_length
    );
    if (result != ASTRA_FDT_OK ||
        !platform_string_list_contains(
            compatible, compatible_length,
            "arm,pl011", sizeof "arm,pl011" - 1u
        )) {
        return false;
    }

    if (!platform_cell_count(
            &view, parent_path, parent_path_length,
            "#address-cells", sizeof "#address-cells" - 1u,
            (uint32_t)PLATFORM_DEFAULT_ADDRESS_CELLS, &address_cells
        ) ||
        !platform_cell_count(
            &view, parent_path, parent_path_length,
            "#size-cells", sizeof "#size-cells" - 1u,
            (uint32_t)PLATFORM_DEFAULT_SIZE_CELLS, &size_cells
        )) {
        return false;
    }

    result = astra_fdt_node_property(
        &view, node_path, node_path_length,
        "reg", sizeof "reg" - 1u, &reg, &reg_length
    );
    if (result != ASTRA_FDT_OK) {
        return false;
    }

    tuple_length = ((size_t)address_cells + (size_t)size_cells) * 4u;
    if (reg_length < tuple_length ||
        !platform_read_cells(reg, reg_length, address_cells, &base) ||
        !platform_read_cells(
            &reg[(size_t)address_cells * 4u],
            reg_length - ((size_t)address_cells * 4u),
            size_cells,
            &register_size
        )) {
        return false;
    }

    if (base == 0u ||
        (base % sizeof(uint32_t)) != 0u ||
        register_size < (uint64_t)PLATFORM_PL011_REGISTER_BYTES ||
        base > UINT64_MAX - register_size ||
        base > (uint64_t)UINTPTR_MAX -
            (uint64_t)(PLATFORM_PL011_REGISTER_BYTES - 1u)) {
        return false;
    }

    *base_out = base;
    return true;
}

bool astra_platform_find_pl011_uart(
    const AstraBootInfo *boot_info,
    uint64_t *base_out
) {
    uint64_t fdt_base = 0u;
    AstraAcpiSpcr spcr = {0};

    if (base_out == NULL) {
        return false;
    }
    if (astra_platform_find_pl011_uart_from_fdt(boot_info, &fdt_base)) {
        *base_out = fdt_base;
        return true;
    }
    if (astra_acpi_find_pl011_uart(boot_info, &spcr)) {
        *base_out = spcr.base_address;
        return true;
    }
    return false;
}
