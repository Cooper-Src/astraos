#ifndef ASTRAOS_FDT_H
#define ASTRAOS_FDT_H

#include <stddef.h>
#include <stdint.h>

enum {
    ASTRA_FDT_HEADER_SIZE = 40u,
    ASTRA_FDT_MAX_SIZE = 64u * 1024u * 1024u
};

#define ASTRA_FDT_MAGIC UINT32_C(0xd00dfeed)

typedef enum AstraFdtResult {
    ASTRA_FDT_OK = 0,
    ASTRA_FDT_INVALID_ARGUMENT,
    ASTRA_FDT_TRUNCATED,
    ASTRA_FDT_BAD_MAGIC,
    ASTRA_FDT_TOO_LARGE,
    ASTRA_FDT_UNSUPPORTED_VERSION,
    ASTRA_FDT_INVALID_BLOCK,
    ASTRA_FDT_INVALID_RESERVE_MAP,
    ASTRA_FDT_INVALID_STRUCTURE,
    ASTRA_FDT_INVALID_STRINGS,
    ASTRA_FDT_PROPERTY_NOT_FOUND
} AstraFdtResult;

/*
 * Borrowed view of an FDT blob. The owner must keep data valid for the view's
 * lifetime. Offsets are byte offsets into data; no allocation is performed.
 */
typedef struct AstraFdtView {
    const uint8_t *data;
    size_t total_size;
    size_t structure_offset;
    size_t structure_size;
    size_t strings_offset;
    size_t strings_size;
    size_t reserve_map_offset;
    size_t reserve_map_size;
} AstraFdtView;

/*
 * Validates the FDT v17-compatible header and all top-level block boundaries.
 * data is a borrowed byte span of available bytes. view_out is unchanged on
 * failure; on success it receives a view into data.
 */
AstraFdtResult astra_fdt_open(
    const void *data,
    size_t available,
    AstraFdtView *view_out
);

/*
 * Returns a borrowed span for a root-node property. name is a byte span and
 * need not be NUL-terminated. Output values are reset on failure.
 */
AstraFdtResult astra_fdt_root_property(
    const AstraFdtView *view,
    const char *name,
    size_t name_length,
    const uint8_t **value_out,
    size_t *value_length_out
);

const char *astra_fdt_result_name(AstraFdtResult result);

#endif
