#include "fdt.h"

#include <stdbool.h>

enum {
    FDT_VERSION_MINIMUM = 17u,
    FDT_VERSION_LAST_COMPATIBLE_MAXIMUM = 17u,
    FDT_RESERVE_ENTRY_SIZE = 16u,
    FDT_TOKEN_SIZE = 4u,
    FDT_TOKEN_BEGIN_NODE = 1u,
    FDT_TOKEN_END_NODE = 2u,
    FDT_TOKEN_PROPERTY = 3u,
    FDT_TOKEN_NOP = 4u,
    FDT_TOKEN_END = 9u
};

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
        ((uint32_t)bytes[1] << 16) |
        ((uint32_t)bytes[2] << 8) |
        (uint32_t)bytes[3];
}

static bool range_inside(size_t total, size_t offset, size_t length) {
    return offset <= total && length <= total - offset;
}

static bool ranges_overlap(size_t left, size_t left_size, size_t right, size_t right_size) {
    if (left_size == 0u || right_size == 0u) {
        return false;
    }
    return left < right + right_size && right < left + left_size;
}

static bool align4(size_t position, size_t *aligned_out) {
    size_t padding = (4u - (position % 4u)) % 4u;

    if (aligned_out == NULL || position > SIZE_MAX - padding) {
        return false;
    }
    *aligned_out = position + padding;
    return true;
}

static bool reserve_entry_is_terminator(const uint8_t *entry) {
    for (size_t i = 0; i < (size_t)FDT_RESERVE_ENTRY_SIZE; i++) {
        if (entry[i] != 0u) {
            return false;
        }
    }
    return true;
}

static bool bytes_equal_text(
    const uint8_t *bytes,
    size_t length,
    const char *text,
    size_t text_length
) {
    if (bytes == NULL || text == NULL || length != text_length) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (bytes[i] != (uint8_t)text[i]) {
            return false;
        }
    }
    return true;
}

static AstraFdtResult validate_structure_block(
    const uint8_t *data,
    size_t total_size,
    size_t structure_offset,
    size_t structure_size,
    size_t strings_offset,
    size_t strings_size
) {
    size_t position = structure_offset;
    size_t structure_end = structure_offset + structure_size;
    size_t depth = 0u;
    bool root_started = false;
    bool root_closed = false;
    bool saw_end = false;

    if (data == NULL ||
        !range_inside(total_size, structure_offset, structure_size) ||
        !range_inside(total_size, strings_offset, strings_size)) {
        return ASTRA_FDT_INVALID_BLOCK;
    }

    while (position < structure_end) {
        uint32_t token = 0u;

        if (!range_inside(structure_end, position, (size_t)FDT_TOKEN_SIZE)) {
            return ASTRA_FDT_INVALID_STRUCTURE;
        }
        token = read_be32(&data[position]);
        position += (size_t)FDT_TOKEN_SIZE;

        if (token == (uint32_t)FDT_TOKEN_BEGIN_NODE) {
            if (root_closed || (depth == 0u && root_started)) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            if (!root_started) {
                if (position >= structure_end || data[position] != 0u) {
                    return ASTRA_FDT_INVALID_STRUCTURE;
                }
                root_started = true;
            }

            while (position < structure_end && data[position] != 0u) {
                position++;
            }
            if (position >= structure_end) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            position++;
            if (!align4(position, &position) || position > structure_end) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            depth++;
        } else if (token == (uint32_t)FDT_TOKEN_END_NODE) {
            if (depth == 0u) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            depth--;
            if (depth == 0u) {
                root_closed = true;
            }
        } else if (token == (uint32_t)FDT_TOKEN_PROPERTY) {
            size_t value_length = 0u;
            size_t name_offset = 0u;
            size_t name_length = 0u;
            size_t name_start = 0u;

            if (!root_started || root_closed || depth == 0u ||
                !range_inside(structure_end, position, 8u)) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            value_length = (size_t)read_be32(&data[position]);
            name_offset = (size_t)read_be32(&data[position + 4u]);
            position += 8u;

            if (!range_inside(structure_end, position, value_length)) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            position += value_length;
            if (!align4(position, &position) || position > structure_end) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            if (name_offset >= strings_size) {
                return ASTRA_FDT_INVALID_STRINGS;
            }

            name_start = strings_offset + name_offset;
            while (name_length < strings_size - name_offset &&
                data[name_start + name_length] != 0u) {
                name_length++;
            }
            if (name_length == 0u || name_length >= strings_size - name_offset) {
                return ASTRA_FDT_INVALID_STRINGS;
            }
        } else if (token == (uint32_t)FDT_TOKEN_NOP) {
            continue;
        } else if (token == (uint32_t)FDT_TOKEN_END) {
            if (!root_started || !root_closed || depth != 0u ||
                position != structure_end) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            saw_end = true;
            break;
        } else {
            return ASTRA_FDT_INVALID_STRUCTURE;
        }
    }

    return saw_end ? ASTRA_FDT_OK : ASTRA_FDT_INVALID_STRUCTURE;
}

AstraFdtResult astra_fdt_open(
    const void *data,
    size_t available,
    AstraFdtView *view_out
) {
    const uint8_t *bytes = (const uint8_t *)data;
    size_t reserve_position = 0;
    size_t reserve_size = 0;
    size_t total_size = 0;
    size_t structure_offset = 0;
    size_t structure_size = 0;
    size_t strings_offset = 0;
    size_t strings_size = 0;
    size_t reserve_offset = 0;
    uint32_t version = 0;
    uint32_t last_compatible_version = 0;
    bool found_reserve_terminator = false;
    AstraFdtResult structure_result = ASTRA_FDT_OK;

    if (data == NULL || view_out == NULL) {
        return ASTRA_FDT_INVALID_ARGUMENT;
    }
    if (available < (size_t)ASTRA_FDT_HEADER_SIZE) {
        return ASTRA_FDT_TRUNCATED;
    }
    if (read_be32(&bytes[0]) != ASTRA_FDT_MAGIC) {
        return ASTRA_FDT_BAD_MAGIC;
    }

    total_size = (size_t)read_be32(&bytes[4]);
    if (total_size < (size_t)ASTRA_FDT_HEADER_SIZE) {
        return ASTRA_FDT_INVALID_BLOCK;
    }
    if (total_size > (size_t)ASTRA_FDT_MAX_SIZE) {
        return ASTRA_FDT_TOO_LARGE;
    }
    if (total_size > available) {
        return ASTRA_FDT_TRUNCATED;
    }

    structure_offset = (size_t)read_be32(&bytes[8]);
    strings_offset = (size_t)read_be32(&bytes[12]);
    reserve_offset = (size_t)read_be32(&bytes[16]);
    version = read_be32(&bytes[20]);
    last_compatible_version = read_be32(&bytes[24]);
    strings_size = (size_t)read_be32(&bytes[32]);
    structure_size = (size_t)read_be32(&bytes[36]);

    if (version < (uint32_t)FDT_VERSION_MINIMUM ||
        last_compatible_version > version ||
        last_compatible_version > (uint32_t)FDT_VERSION_LAST_COMPATIBLE_MAXIMUM) {
        return ASTRA_FDT_UNSUPPORTED_VERSION;
    }

    if (structure_offset < (size_t)ASTRA_FDT_HEADER_SIZE ||
        (structure_offset % (size_t)FDT_TOKEN_SIZE) != 0u ||
        structure_size == 0u ||
        !range_inside(total_size, structure_offset, structure_size) ||
        strings_offset < (size_t)ASTRA_FDT_HEADER_SIZE ||
        !range_inside(total_size, strings_offset, strings_size) ||
        reserve_offset < (size_t)ASTRA_FDT_HEADER_SIZE ||
        (reserve_offset % 8u) != 0u ||
        reserve_offset > total_size) {
        return ASTRA_FDT_INVALID_BLOCK;
    }

    reserve_position = reserve_offset;
    while (range_inside(total_size, reserve_position, (size_t)FDT_RESERVE_ENTRY_SIZE)) {
        if (reserve_entry_is_terminator(&bytes[reserve_position])) {
            reserve_position += (size_t)FDT_RESERVE_ENTRY_SIZE;
            found_reserve_terminator = true;
            break;
        }
        reserve_position += (size_t)FDT_RESERVE_ENTRY_SIZE;
    }
    if (!found_reserve_terminator) {
        return ASTRA_FDT_INVALID_RESERVE_MAP;
    }
    reserve_size = reserve_position - reserve_offset;

    if (ranges_overlap(structure_offset, structure_size, strings_offset, strings_size) ||
        ranges_overlap(structure_offset, structure_size, reserve_offset, reserve_size) ||
        ranges_overlap(strings_offset, strings_size, reserve_offset, reserve_size)) {
        return ASTRA_FDT_INVALID_BLOCK;
    }

    structure_result = validate_structure_block(
        bytes,
        total_size,
        structure_offset,
        structure_size,
        strings_offset,
        strings_size
    );
    if (structure_result != ASTRA_FDT_OK) {
        return structure_result;
    }

    view_out->data = bytes;
    view_out->total_size = total_size;
    view_out->structure_offset = structure_offset;
    view_out->structure_size = structure_size;
    view_out->strings_offset = strings_offset;
    view_out->strings_size = strings_size;
    view_out->reserve_map_offset = reserve_offset;
    view_out->reserve_map_size = reserve_size;
    return ASTRA_FDT_OK;
}

AstraFdtResult astra_fdt_root_property(
    const AstraFdtView *view,
    const char *name,
    size_t name_length,
    const uint8_t **value_out,
    size_t *value_length_out
) {
    AstraFdtView checked_view;
    AstraFdtResult result = ASTRA_FDT_OK;
    size_t position = 0;
    size_t structure_end = 0;
    size_t depth = 0;
    bool root_seen = false;

    if (view == NULL || name == NULL || name_length == 0u ||
        value_out == NULL || value_length_out == NULL) {
        return ASTRA_FDT_INVALID_ARGUMENT;
    }
    *value_out = NULL;
    *value_length_out = 0;
    result = astra_fdt_open(view->data, view->total_size, &checked_view);
    if (result != ASTRA_FDT_OK) {
        return result;
    }

    position = checked_view.structure_offset;
    structure_end = checked_view.structure_offset + checked_view.structure_size;
    while (position < structure_end) {
        uint32_t token = 0;

        if (!range_inside(structure_end, position, (size_t)FDT_TOKEN_SIZE)) {
            return ASTRA_FDT_INVALID_STRUCTURE;
        }
        token = read_be32(&checked_view.data[position]);
        position += (size_t)FDT_TOKEN_SIZE;

        if (token == (uint32_t)FDT_TOKEN_BEGIN_NODE) {
            if (depth == 0u) {
                if (root_seen) {
                    return ASTRA_FDT_INVALID_STRUCTURE;
                }
                root_seen = true;
            }
            while (position < structure_end && checked_view.data[position] != 0u) {
                position++;
            }
            if (position == structure_end) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            position++;
            if (!align4(position, &position) || position > structure_end) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            depth++;
        } else if (token == (uint32_t)FDT_TOKEN_END_NODE) {
            if (depth == 0u) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            depth--;
            if (root_seen && depth == 0u) {
                return ASTRA_FDT_PROPERTY_NOT_FOUND;
            }
        } else if (token == (uint32_t)FDT_TOKEN_PROPERTY) {
            size_t value_length = 0;
            size_t name_offset = 0;
            size_t property_name_length = 0;
            size_t property_name_start = 0;
            const uint8_t *property_name = NULL;
            const uint8_t *property_value = NULL;

            if (depth == 0u ||
                !range_inside(structure_end, position, 8u)) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            value_length = (size_t)read_be32(&checked_view.data[position]);
            name_offset = (size_t)read_be32(&checked_view.data[position + 4u]);
            position += 8u;
            if (!range_inside(structure_end, position, value_length)) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }

            property_value = &checked_view.data[position];
            position += value_length;
            if (!align4(position, &position) || position > structure_end) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            if (name_offset >= checked_view.strings_size) {
                return ASTRA_FDT_INVALID_STRINGS;
            }

            property_name_start = checked_view.strings_offset + name_offset;
            property_name_length = 0;
            while (name_offset + property_name_length < checked_view.strings_size &&
                checked_view.data[property_name_start + property_name_length] != 0u) {
                property_name_length++;
            }
            if (name_offset + property_name_length >= checked_view.strings_size) {
                return ASTRA_FDT_INVALID_STRINGS;
            }

            property_name = &checked_view.data[property_name_start];
            if (depth == 1u &&
                bytes_equal_text(property_name, property_name_length, name, name_length)) {
                *value_out = property_value;
                *value_length_out = value_length;
                return ASTRA_FDT_OK;
            }
        } else if (token == (uint32_t)FDT_TOKEN_NOP) {
            continue;
        } else if (token == (uint32_t)FDT_TOKEN_END) {
            if (!root_seen || depth != 0u) {
                return ASTRA_FDT_INVALID_STRUCTURE;
            }
            return ASTRA_FDT_PROPERTY_NOT_FOUND;
        } else {
            return ASTRA_FDT_INVALID_STRUCTURE;
        }
    }
    return ASTRA_FDT_INVALID_STRUCTURE;
}

const char *astra_fdt_result_name(AstraFdtResult result) {
    switch (result) {
        case ASTRA_FDT_OK:
            return "success";
        case ASTRA_FDT_INVALID_ARGUMENT:
            return "invalid argument";
        case ASTRA_FDT_TRUNCATED:
            return "truncated FDT";
        case ASTRA_FDT_BAD_MAGIC:
            return "invalid FDT magic";
        case ASTRA_FDT_TOO_LARGE:
            return "FDT exceeds the size limit";
        case ASTRA_FDT_UNSUPPORTED_VERSION:
            return "unsupported FDT version";
        case ASTRA_FDT_INVALID_BLOCK:
            return "invalid FDT block range";
        case ASTRA_FDT_INVALID_RESERVE_MAP:
            return "invalid FDT reserve map";
        case ASTRA_FDT_INVALID_STRUCTURE:
            return "invalid FDT structure block";
        case ASTRA_FDT_INVALID_STRINGS:
            return "invalid FDT strings block";
        case ASTRA_FDT_PROPERTY_NOT_FOUND:
            return "root property not found";
    }
    return "unknown FDT error";
}
