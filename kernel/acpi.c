#include "acpi.h"

#include <stddef.h>
#include <stdint.h>

enum {
    ACPI_RSDP_V1_SIZE = 20u,
    ACPI_RSDP_V2_MIN_SIZE = 36u,
    ACPI_SDT_HEADER_SIZE = 36u,
    ACPI_SPCR_MIN_SIZE = 80u,
    ACPI_MAX_TABLE_SIZE = 1024u * 1024u,
    ACPI_MEMORY_DESCRIPTOR_MIN_SIZE = 40u,
    ACPI_MEMORY_TYPE_OFFSET = 0u,
    ACPI_MEMORY_PHYSICAL_START_OFFSET = 8u,
    ACPI_MEMORY_NUMBER_OF_PAGES_OFFSET = 24u,
    ACPI_MEMORY_PAGE_SIZE = 4096u,
    ACPI_MEMORY_RECLAIM_TYPE = 9u,
    ACPI_MEMORY_NVS_TYPE = 10u,
    ACPI_RSDP_REVISION_OFFSET = 15u,
    ACPI_RSDP_RSDT_OFFSET = 16u,
    ACPI_RSDP_LENGTH_OFFSET = 20u,
    ACPI_RSDP_XSDT_OFFSET = 24u,
    ACPI_SDT_LENGTH_OFFSET = 4u,
    ACPI_SDT_CHECKSUM_OFFSET = 9u,
    ACPI_SPCR_INTERFACE_OFFSET = 36u,
    ACPI_SPCR_GAS_OFFSET = 40u,
    ACPI_SPCR_BASE_OFFSET = 44u,
    ACPI_SPCR_PL011_INTERFACE = 3u,
    ACPI_GAS_SYSTEM_MEMORY = 0u,
    ACPI_GAS_REGISTER_WIDTH_32 = 32u,
    ACPI_GAS_DWORD_ACCESS = 3u,
    ACPI_PL011_REGISTER_BYTES = 72u
};

static uint32_t acpi_read_u32_le(const uint8_t *bytes) {
    return (uint32_t)bytes[0]
        | ((uint32_t)bytes[1] << 8u)
        | ((uint32_t)bytes[2] << 16u)
        | ((uint32_t)bytes[3] << 24u);
}

static uint64_t acpi_read_u64_le(const uint8_t *bytes) {
    uint64_t value = 0u;

    for (size_t i = 0u; i < 8u; i++) {
        value |= (uint64_t)bytes[i] << (i * 8u);
    }
    return value;
}

static bool acpi_bytes_equal(const uint8_t *bytes, const char text[4]) {
    if (bytes == NULL || text == NULL) {
        return false;
    }
    for (size_t i = 0u; i < 4u; i++) {
        if (bytes[i] != (uint8_t)text[i]) {
            return false;
        }
    }
    return true;
}

static bool acpi_checksum_valid(const uint8_t *bytes, size_t length) {
    uint8_t sum = 0u;

    if (bytes == NULL || length == 0u) {
        return false;
    }
    for (size_t i = 0u; i < length; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum == 0u;
}

/* Physical ACPI addresses are dereferenced only after their full range has
 * been shown to fit in exactly one ACPI-reclaim or ACPI-NVS descriptor. */
static bool acpi_region_for_address(
    const AstraBootInfo *boot_info,
    uint64_t address,
    size_t required_size,
    const uint8_t **region_out,
    size_t *available_out
) {
    const uint8_t *map = NULL;
    size_t map_size = 0u;
    size_t descriptor_size = 0u;
    bool found = false;
    uint64_t available = 0u;

    if (region_out == NULL || available_out == NULL) {
        return false;
    }
    *region_out = NULL;
    *available_out = 0u;

    if (boot_info == NULL ||
        boot_info->magic != ASTRA_BOOT_INFO_MAGIC ||
        boot_info->version != ASTRA_BOOT_INFO_VERSION ||
        boot_info->memory_map == NULL ||
        boot_info->memory_map_size == 0u ||
        boot_info->memory_map_size > ASTRA_BOOT_MEMORY_MAP_CAP_BYTES ||
        boot_info->memory_map_size > (uint64_t)SIZE_MAX ||
        boot_info->memory_descriptor_size < (uint64_t)ACPI_MEMORY_DESCRIPTOR_MIN_SIZE ||
        boot_info->memory_descriptor_size > (uint64_t)SIZE_MAX ||
        boot_info->memory_descriptor_size > boot_info->memory_map_size ||
        boot_info->memory_map_size % boot_info->memory_descriptor_size != 0u ||
        address == 0u ||
        address > (uint64_t)UINTPTR_MAX) {
        return false;
    }

    map = (const uint8_t *)boot_info->memory_map;
    map_size = (size_t)boot_info->memory_map_size;
    descriptor_size = (size_t)boot_info->memory_descriptor_size;

    for (size_t offset = 0u; offset < map_size; offset += descriptor_size) {
        const uint8_t *descriptor = &map[offset];
        uint32_t type = acpi_read_u32_le(&descriptor[ACPI_MEMORY_TYPE_OFFSET]);
        uint64_t base = acpi_read_u64_le(
            &descriptor[ACPI_MEMORY_PHYSICAL_START_OFFSET]
        );
        uint64_t pages = acpi_read_u64_le(
            &descriptor[ACPI_MEMORY_NUMBER_OF_PAGES_OFFSET]
        );
        uint64_t span = 0u;
        uint64_t end = 0u;

        if (pages == 0u) {
            continue;
        }
        if (pages > (UINT64_MAX - base) / (uint64_t)ACPI_MEMORY_PAGE_SIZE) {
            return false;
        }
        span = pages * (uint64_t)ACPI_MEMORY_PAGE_SIZE;
        end = base + span;
        if (address < base || address >= end) {
            continue;
        }

        if (found ||
            (type != (uint32_t)ACPI_MEMORY_RECLAIM_TYPE &&
             type != (uint32_t)ACPI_MEMORY_NVS_TYPE)) {
            return false;
        }
        found = true;
        available = end - address;
    }

    if (!found || available > (uint64_t)SIZE_MAX ||
        (uint64_t)required_size > available) {
        return false;
    }

    *region_out = (const uint8_t *)(uintptr_t)address;
    *available_out = (size_t)available;
    return true;
}

static bool acpi_read_sdt(
    const AstraBootInfo *boot_info,
    uint64_t address,
    const char expected_signature[4],
    const uint8_t **table_out,
    size_t *table_size_out
) {
    const uint8_t *table = NULL;
    size_t available = 0u;
    size_t table_size = 0u;

    if (table_out == NULL || table_size_out == NULL ||
        !acpi_region_for_address(
            boot_info, address, (size_t)ACPI_SDT_HEADER_SIZE,
            &table, &available
        )) {
        return false;
    }
    if (!acpi_bytes_equal(table, expected_signature)) {
        return false;
    }

    table_size = (size_t)acpi_read_u32_le(&table[ACPI_SDT_LENGTH_OFFSET]);
    if (table_size < (size_t)ACPI_SDT_HEADER_SIZE ||
        table_size > (size_t)ACPI_MAX_TABLE_SIZE ||
        table_size > available ||
        !acpi_checksum_valid(table, table_size)) {
        return false;
    }

    *table_out = table;
    *table_size_out = table_size;
    return true;
}

static bool acpi_find_spcr_in_root(
    const AstraBootInfo *boot_info,
    uint64_t root_address,
    bool is_xsdt,
    AstraAcpiSpcr *spcr_out
) {
    const uint8_t *root = NULL;
    size_t root_size = 0u;
    size_t entry_size = is_xsdt ? 8u : 4u;
    size_t entries_offset = (size_t)ACPI_SDT_HEADER_SIZE;

    if (!acpi_read_sdt(
            boot_info, root_address, is_xsdt ? "XSDT" : "RSDT",
            &root, &root_size
        ) ||
        (root_size - entries_offset) % entry_size != 0u) {
        return false;
    }

    for (size_t offset = entries_offset; offset < root_size; offset += entry_size) {
        uint64_t table_address = is_xsdt
            ? acpi_read_u64_le(&root[offset])
            : (uint64_t)acpi_read_u32_le(&root[offset]);
        const uint8_t *table = NULL;
        size_t table_size = 0u;
        uint64_t base_address = 0u;

        if (table_address == 0u) {
            continue;
        }

        /* Ignore unrelated tables, but never dereference an unvalidated pointer. */
        if (!acpi_region_for_address(
                boot_info, table_address, (size_t)ACPI_SDT_HEADER_SIZE,
                &table, &table_size
            ) ||
            !acpi_bytes_equal(table, "SPCR")) {
            continue;
        }

        if (!acpi_read_sdt(
                boot_info, table_address, "SPCR", &table, &table_size
            ) ||
            table_size < (size_t)ACPI_SPCR_MIN_SIZE ||
            table[ACPI_SPCR_INTERFACE_OFFSET] !=
                (uint8_t)ACPI_SPCR_PL011_INTERFACE ||
            table[ACPI_SPCR_GAS_OFFSET] != (uint8_t)ACPI_GAS_SYSTEM_MEMORY ||
            table[ACPI_SPCR_GAS_OFFSET + 1u] !=
                (uint8_t)ACPI_GAS_REGISTER_WIDTH_32 ||
            table[ACPI_SPCR_GAS_OFFSET + 2u] != 0u ||
            (table[ACPI_SPCR_GAS_OFFSET + 3u] != 0u &&
             table[ACPI_SPCR_GAS_OFFSET + 3u] !=
                 (uint8_t)ACPI_GAS_DWORD_ACCESS)) {
            return false;
        }

        base_address = acpi_read_u64_le(&table[ACPI_SPCR_BASE_OFFSET]);
        if (base_address == 0u ||
            (base_address % sizeof(uint32_t)) != 0u ||
            base_address > UINT64_MAX - (uint64_t)ACPI_PL011_REGISTER_BYTES ||
            base_address > (uint64_t)UINTPTR_MAX -
                (uint64_t)(ACPI_PL011_REGISTER_BYTES - 1u)) {
            return false;
        }

        spcr_out->base_address = base_address;
        return true;
    }

    return false;
}

bool astra_acpi_find_pl011_uart(
    const AstraBootInfo *boot_info,
    AstraAcpiSpcr *spcr_out
) {
    const uint8_t *rsdp = NULL;
    size_t rsdp_available = 0u;
    size_t rsdp_length = (size_t)ACPI_RSDP_V1_SIZE;
    uint8_t revision = 0u;
    bool is_xsdt = false;
    uint64_t root_address = 0u;
    AstraAcpiSpcr parsed = {0};

    if (boot_info == NULL || spcr_out == NULL ||
        boot_info->magic != ASTRA_BOOT_INFO_MAGIC ||
        boot_info->version != ASTRA_BOOT_INFO_VERSION ||
        boot_info->acpi_rsdp_base == 0u ||
        !acpi_region_for_address(
            boot_info, boot_info->acpi_rsdp_base,
            (size_t)ACPI_RSDP_V1_SIZE, &rsdp, &rsdp_available
        )) {
        return false;
    }

    /* The first eight bytes include the full ACPI RSDP signature. */
    if (rsdp[0] != (uint8_t)'R' || rsdp[1] != (uint8_t)'S' ||
        rsdp[2] != (uint8_t)'D' || rsdp[3] != (uint8_t)' ' ||
        rsdp[4] != (uint8_t)'P' || rsdp[5] != (uint8_t)'T' ||
        rsdp[6] != (uint8_t)'R' || rsdp[7] != 0u ||
        !acpi_checksum_valid(rsdp, (size_t)ACPI_RSDP_V1_SIZE)) {
        return false;
    }

    revision = rsdp[ACPI_RSDP_REVISION_OFFSET];
    if (revision >= 2u) {
        if (rsdp_available < (size_t)ACPI_RSDP_V2_MIN_SIZE) {
            return false;
        }
        rsdp_length = (size_t)acpi_read_u32_le(&rsdp[ACPI_RSDP_LENGTH_OFFSET]);
        if (rsdp_length < (size_t)ACPI_RSDP_V2_MIN_SIZE ||
            rsdp_length > (size_t)ACPI_MAX_TABLE_SIZE ||
            rsdp_length > rsdp_available ||
            !acpi_checksum_valid(rsdp, rsdp_length)) {
            return false;
        }
        root_address = acpi_read_u64_le(&rsdp[ACPI_RSDP_XSDT_OFFSET]);
        is_xsdt = root_address != 0u;
        if (root_address == 0u) {
            root_address = (uint64_t)acpi_read_u32_le(
                &rsdp[ACPI_RSDP_RSDT_OFFSET]
            );
        }
    } else {
        root_address = (uint64_t)acpi_read_u32_le(&rsdp[ACPI_RSDP_RSDT_OFFSET]);
    }

    if (root_address == 0u ||
        !acpi_find_spcr_in_root(boot_info, root_address, is_xsdt, &parsed)) {
        return false;
    }

    *spcr_out = parsed;
    return true;
}
