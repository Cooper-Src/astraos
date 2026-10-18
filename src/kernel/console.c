#include "console.h"

#include <stddef.h>
#include <stdint.h>

#include "limine.h"
#include "font8x8_basic.h"

enum {
    UART_DR = 0x000,
    UART_FR = 0x018,
    UART_IBRD = 0x024,
    UART_FBRD = 0x028,
    UART_LCR_H = 0x02c,
    UART_CR = 0x030,
    UART_IMSC = 0x038,
    UART_ICR = 0x044,
    UART_FR_RXFE = 1u << 4,
    UART_FR_TXFF = 1u << 5,
    UART_CR_ENABLE = 1u,
    UART_CR_TX_ENABLE = 1u << 8,
    UART_CR_RX_ENABLE = 1u << 9,
    UART_LCR_8BIT_FIFO = 0x70,
    UART_INTERRUPT_MASK = 0x7ff,
    FONT_WIDTH = 8,
    FONT_HEIGHT = 8,
    SCROLL_POLL_LIMIT = 1000000
};

typedef struct {
    uint32_t address_cells;
    uint32_t size_cells;
    uint32_t parent_address_cells;
    uint32_t parent_size_cells;
    bool pl011;
    bool disabled;
    uint8_t reg[32];
    size_t reg_length;
} FdtNode;

static volatile uint8_t *g_framebuffer = NULL;
static size_t g_framebuffer_size = 0;
static size_t g_framebuffer_pitch = 0;
static uint64_t g_framebuffer_width = 0;
static uint64_t g_framebuffer_height = 0;
static uint8_t g_red_size = 0;
static uint8_t g_red_shift = 0;
static uint8_t g_green_size = 0;
static uint8_t g_green_shift = 0;
static uint8_t g_blue_size = 0;
static uint8_t g_blue_shift = 0;
static bool g_framebuffer_ready = false;
static uintptr_t g_uart_base = 0;
static bool g_uart_ready = false;
static uint64_t g_hhdm_offset = 0;
static uint64_t g_cursor_x = 0;
static uint64_t g_cursor_y = 0;
static const uint32_t COLOR_BACKGROUND = 0x071321u;
static const uint32_t COLOR_FOREGROUND = 0xd8e6f2u;
static const uint32_t COLOR_ACCENT = 0x56d6c2u;

static bool add_overflow_size(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) {
        return true;
    }
    *out = left + right;
    return false;
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24u) |
           ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) |
           (uint32_t)bytes[3];
}

static bool bytes_equal(const uint8_t *bytes, size_t length, const char *text) {
    size_t text_length = 0;

    if (bytes == NULL || text == NULL) {
        return false;
    }
    while (text[text_length] != '\0') {
        text_length++;
    }
    if (length != text_length) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (bytes[i] != (uint8_t)text[i]) {
            return false;
        }
    }
    return true;
}

static bool compatible_has_pl011(const uint8_t *bytes, size_t length) {
    size_t position = 0;

    while (position < length) {
        size_t end = position;

        while (end < length && bytes[end] != 0u) {
            end++;
        }
        if (end == length) {
            return false;
        }
        if (bytes_equal(bytes + position, end - position, "arm,pl011")) {
            return true;
        }
        position = end + 1u;
    }
    return false;
}

static bool fdt_read_cells(const uint8_t *bytes, size_t cell_count, uint64_t *out_value) {
    uint64_t value = 0;

    if (bytes == NULL || out_value == NULL || cell_count == 0u || cell_count > 2u) {
        return false;
    }
    for (size_t i = 0; i < cell_count; i++) {
        value = (value << 32u) | (uint64_t)read_be32(bytes + i * 4u);
    }
    *out_value = value;
    return true;
}

/* Reads only bounded FDT regions and returns the first enabled arm,pl011 register base. */
static bool fdt_find_pl011(const void *blob, uint64_t hhdm_offset, uintptr_t *out_address) {
    const uint8_t *bytes = (const uint8_t *)blob;
    FdtNode stack[32] = {0};
    size_t total_size = 0;
    size_t structure_offset = 0;
    size_t structure_size = 0;
    size_t strings_offset = 0;
    size_t strings_size = 0;
    size_t position = 0;
    size_t structure_end = 0;
    size_t depth = 0;

    if (bytes == NULL || out_address == NULL) {
        return false;
    }
    if (read_be32(bytes) != 0xd00dfeedu) {
        return false;
    }
    total_size = (size_t)read_be32(bytes + 4u);
    structure_offset = (size_t)read_be32(bytes + 8u);
    strings_offset = (size_t)read_be32(bytes + 12u);
    strings_size = (size_t)read_be32(bytes + 32u);
    structure_size = (size_t)read_be32(bytes + 36u);

    if (total_size < 40u || structure_offset > total_size ||
        structure_size > total_size - structure_offset ||
        strings_offset > total_size || strings_size > total_size - strings_offset) {
        return false;
    }
    structure_end = structure_offset + structure_size;
    position = structure_offset;

    while (position <= structure_end && structure_end - position >= 4u) {
        uint32_t token = read_be32(bytes + position);
        position += 4u;

        if (token == 1u) {
            size_t name_end = position;

            if (depth >= (sizeof stack / sizeof stack[0])) {
                return false;
            }
            while (name_end < structure_end && bytes[name_end] != 0u) {
                name_end++;
            }
            if (name_end >= structure_end) {
                return false;
            }
            if (depth == 0u) {
                stack[depth].parent_address_cells = 2u;
                stack[depth].parent_size_cells = 1u;
            } else {
                stack[depth].parent_address_cells = stack[depth - 1u].address_cells;
                stack[depth].parent_size_cells = stack[depth - 1u].size_cells;
            }
            stack[depth].address_cells = stack[depth].parent_address_cells;
            stack[depth].size_cells = stack[depth].parent_size_cells;
            stack[depth].pl011 = false;
            stack[depth].disabled = false;
            stack[depth].reg_length = 0;
            for (size_t i = 0; i < sizeof stack[depth].reg; i++) {
                stack[depth].reg[i] = 0u;
            }
            depth++;
            position = (name_end + 4u) & ~(size_t)3u;
            if (position > structure_end) {
                return false;
            }
        } else if (token == 2u) {
            FdtNode *node = NULL;

            if (depth == 0u) {
                return false;
            }
            node = &stack[depth - 1u];
            if (node->pl011 && !node->disabled && node->reg_length >= 4u) {
                uint64_t physical_address = 0;
                uint64_t virtual_address = 0;
                size_t address_cells = (size_t)node->parent_address_cells;
                size_t size_cells = (size_t)node->parent_size_cells;
                size_t required_cells = 0;
                size_t required_bytes = 0;

                if ((address_cells == 1u || address_cells == 2u) &&
                    size_cells <= 2u &&
                    !add_overflow_size(address_cells, size_cells, &required_cells) &&
                    !add_overflow_size(0u, required_cells * 4u, &required_bytes) &&
                    required_bytes <= node->reg_length &&
                    fdt_read_cells(node->reg, address_cells, &physical_address) &&
                    physical_address <= UINTPTR_MAX &&
                    hhdm_offset <= UINT64_MAX - physical_address) {
                    virtual_address = hhdm_offset + physical_address;
                    if (virtual_address <= UINTPTR_MAX) {
                        *out_address = (uintptr_t)virtual_address;
                        return true;
                    }
                }
            }
            depth--;
        } else if (token == 3u) {
            uint32_t property_length = 0;
            uint32_t name_offset = 0;
            const uint8_t *property = NULL;
            const uint8_t *property_name = NULL;
            size_t property_name_length = 0;
            size_t property_name_end = 0;
            FdtNode *node = NULL;

            if (depth == 0u || structure_end - position < 8u) {
                return false;
            }
            property_length = read_be32(bytes + position);
            name_offset = read_be32(bytes + position + 4u);
            position += 8u;
            if ((size_t)property_length > structure_end - position ||
                (size_t)name_offset >= strings_size) {
                return false;
            }
            property = bytes + position;
            property_name = bytes + strings_offset + (size_t)name_offset;
            property_name_end = (size_t)name_offset;
            while (property_name_end < strings_size &&
                   bytes[strings_offset + property_name_end] != 0u) {
                property_name_end++;
            }
            if (property_name_end >= strings_size) {
                return false;
            }
            property_name_length = property_name_end - (size_t)name_offset;
            node = &stack[depth - 1u];

            if (bytes_equal(property_name, property_name_length, "#address-cells") &&
                property_length >= 4u) {
                node->address_cells = read_be32(property);
            } else if (bytes_equal(property_name, property_name_length, "#size-cells") &&
                       property_length >= 4u) {
                node->size_cells = read_be32(property);
            } else if (bytes_equal(property_name, property_name_length, "compatible")) {
                node->pl011 = compatible_has_pl011(property, (size_t)property_length);
            } else if (bytes_equal(property_name, property_name_length, "status")) {
                node->disabled = bytes_equal(property, (size_t)property_length, "disabled");
            } else if (bytes_equal(property_name, property_name_length, "reg") &&
                       (size_t)property_length <= sizeof node->reg) {
                node->reg_length = (size_t)property_length;
                for (size_t i = 0; i < node->reg_length; i++) {
                    node->reg[i] = property[i];
                }
            }
            position += (size_t)property_length;
            if (position > SIZE_MAX - 3u) {
                return false;
            }
            position = (position + 3u) & ~(size_t)3u;
            if (position > structure_end) {
                return false;
            }
        } else if (token == 4u) {
            continue;
        } else if (token == 9u) {
            return false;
        } else {
            return false;
        }
    }
    return false;
}

static volatile uint32_t *uart_register(size_t offset) {
    return (volatile uint32_t *)(g_uart_base + (uintptr_t)offset);
}

static void uart_put_raw(uint8_t value) {
    volatile uint32_t *flags = uart_register(UART_FR);
    size_t spins = 0;

    while ((*flags & UART_FR_TXFF) != 0u && spins < SCROLL_POLL_LIMIT) {
        spins++;
    }
    if (spins < SCROLL_POLL_LIMIT) {
        *uart_register(UART_DR) = (uint32_t)value;
    }
}

static int uart_try_get(void) {
    volatile uint32_t *flags = uart_register(UART_FR);

    if ((*flags & UART_FR_RXFE) != 0u) {
        return -1;
    }
    return (int)(*uart_register(UART_DR) & 0xffu);
}

static bool framebuffer_valid(const struct limine_framebuffer *framebuffer) {
    uint64_t minimum_pitch = 0;

    if (framebuffer == NULL || framebuffer->address == NULL ||
        framebuffer->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        framebuffer->bpp != 32u || framebuffer->width < FONT_WIDTH ||
        framebuffer->height < FONT_HEIGHT || framebuffer->width > UINT64_MAX / 4u ||
        framebuffer->red_mask_size == 0u || framebuffer->red_mask_size > 8u ||
        framebuffer->green_mask_size == 0u || framebuffer->green_mask_size > 8u ||
        framebuffer->blue_mask_size == 0u || framebuffer->blue_mask_size > 8u ||
        (uint16_t)framebuffer->red_mask_shift + framebuffer->red_mask_size > 32u ||
        (uint16_t)framebuffer->green_mask_shift + framebuffer->green_mask_size > 32u ||
        (uint16_t)framebuffer->blue_mask_shift + framebuffer->blue_mask_size > 32u) {
        return false;
    }
    minimum_pitch = framebuffer->width * 4u;
    if (framebuffer->pitch < minimum_pitch || framebuffer->pitch > SIZE_MAX ||
        framebuffer->height > SIZE_MAX / framebuffer->pitch) {
        return false;
    }
    return true;
}

static uint32_t encode_channel(uint8_t channel, uint8_t size, uint8_t shift) {
    uint32_t maximum = (1u << (unsigned int)size) - 1u;
    uint32_t scaled = ((uint32_t)channel * maximum) / 255u;

    return scaled << (unsigned int)shift;
}

static uint32_t encode_rgb(uint32_t rgb) {
    uint8_t red = (uint8_t)((rgb >> 16u) & 0xffu);
    uint8_t green = (uint8_t)((rgb >> 8u) & 0xffu);
    uint8_t blue = (uint8_t)(rgb & 0xffu);

    return encode_channel(red, g_red_size, g_red_shift) |
           encode_channel(green, g_green_size, g_green_shift) |
           encode_channel(blue, g_blue_size, g_blue_shift);
}

static void framebuffer_pixel(uint64_t x, uint64_t y, uint32_t rgb) {
    volatile uint32_t *row = NULL;

    if (!g_framebuffer_ready || x >= g_framebuffer_width || y >= g_framebuffer_height) {
        return;
    }
    row = (volatile uint32_t *)(g_framebuffer + (size_t)y * g_framebuffer_pitch);
    row[(size_t)x] = encode_rgb(rgb);
}

static void framebuffer_clear_row(uint64_t y) {
    for (uint64_t x = 0; x < g_framebuffer_width; x++) {
        framebuffer_pixel(x, y, COLOR_BACKGROUND);
    }
}

static void framebuffer_scroll(void) {
    size_t shift = g_framebuffer_pitch * FONT_HEIGHT;
    size_t copy_length = g_framebuffer_size - shift;

    for (size_t i = 0; i < copy_length; i++) {
        g_framebuffer[i] = g_framebuffer[i + shift];
    }
    for (uint64_t y = g_framebuffer_height - FONT_HEIGHT; y < g_framebuffer_height; y++) {
        framebuffer_clear_row(y);
    }
    g_cursor_y = g_framebuffer_height / FONT_HEIGHT - 1u;
}

static void framebuffer_newline(void) {
    g_cursor_x = 0;
    g_cursor_y++;
    if ((g_cursor_y + 1u) * FONT_HEIGHT > g_framebuffer_height) {
        framebuffer_scroll();
    }
}

static void framebuffer_put_char(uint8_t character) {
    uint8_t glyph = character < 128u ? character : (uint8_t)'?';

    if (!g_framebuffer_ready) {
        return;
    }
    if (character == (uint8_t)'\n') {
        framebuffer_newline();
        return;
    }
    if (character == (uint8_t)'\r') {
        g_cursor_x = 0;
        return;
    }
    if (character == (uint8_t)'\b') {
        if (g_cursor_x > 0u) {
            g_cursor_x--;
            for (uint64_t y = 0; y < FONT_HEIGHT; y++) {
                for (uint64_t x = 0; x < FONT_WIDTH; x++) {
                    framebuffer_pixel(g_cursor_x * FONT_WIDTH + x,
                                      g_cursor_y * FONT_HEIGHT + y, COLOR_BACKGROUND);
                }
            }
        }
        return;
    }

    if ((g_cursor_x + 1u) * FONT_WIDTH > g_framebuffer_width) {
        framebuffer_newline();
    }
    for (uint64_t row = 0; row < FONT_HEIGHT; row++) {
        uint8_t pattern = font8x8_basic[glyph][(size_t)row];
        for (uint64_t column = 0; column < FONT_WIDTH; column++) {
            uint32_t color = (pattern & (1u << (unsigned int)column)) != 0u
                ? COLOR_FOREGROUND : COLOR_BACKGROUND;
            framebuffer_pixel(g_cursor_x * FONT_WIDTH + column,
                              g_cursor_y * FONT_HEIGHT + row, color);
        }
    }
    g_cursor_x++;
}

void console_init(struct limine_framebuffer_response *framebuffer_response,
                  struct limine_dtb_response *dtb_response, uint64_t hhdm_offset) {
    g_hhdm_offset = hhdm_offset;
    if (framebuffer_response != NULL && framebuffer_response->framebuffer_count > 0u &&
        framebuffer_response->framebuffers != NULL &&
        framebuffer_valid(framebuffer_response->framebuffers[0])) {
        struct limine_framebuffer *framebuffer = framebuffer_response->framebuffers[0];

        g_framebuffer = (volatile uint8_t *)framebuffer->address;
        g_framebuffer_pitch = (size_t)framebuffer->pitch;
        g_framebuffer_size = g_framebuffer_pitch * (size_t)framebuffer->height;
        g_framebuffer_width = framebuffer->width;
        g_framebuffer_height = framebuffer->height;
        g_red_size = framebuffer->red_mask_size;
        g_red_shift = framebuffer->red_mask_shift;
        g_green_size = framebuffer->green_mask_size;
        g_green_shift = framebuffer->green_mask_shift;
        g_blue_size = framebuffer->blue_mask_size;
        g_blue_shift = framebuffer->blue_mask_shift;
        g_framebuffer_ready = true;
        console_clear();
    }

    if (dtb_response != NULL && dtb_response->dtb_ptr != NULL) {
        uintptr_t uart_address = 0;

        if (fdt_find_pl011(dtb_response->dtb_ptr, g_hhdm_offset, &uart_address)) {
            g_uart_base = uart_address;
            *uart_register(UART_CR) = 0u;
            *uart_register(UART_ICR) = UART_INTERRUPT_MASK;
            *uart_register(UART_IBRD) = 13u;
            *uart_register(UART_FBRD) = 1u;
            *uart_register(UART_LCR_H) = UART_LCR_8BIT_FIFO;
            *uart_register(UART_IMSC) = 0u;
            *uart_register(UART_CR) = UART_CR_ENABLE | UART_CR_TX_ENABLE | UART_CR_RX_ENABLE;
            g_uart_ready = true;
        }
    }
}

void console_write_char(char character) {
    uint8_t value = (uint8_t)character;

    if (g_uart_ready) {
        if (value == (uint8_t)'\n') {
            uart_put_raw((uint8_t)'\r');
        }
        uart_put_raw(value);
    }
    framebuffer_put_char(value);
}

void console_write(const char *text) {
    if (text == NULL) {
        return;
    }
    for (size_t i = 0; text[i] != '\0'; i++) {
        console_write_char(text[i]);
    }
}

void console_clear(void) {
    if (!g_framebuffer_ready) {
        return;
    }
    for (uint64_t y = 0; y < g_framebuffer_height; y++) {
        framebuffer_clear_row(y);
    }
    g_cursor_x = 0;
    g_cursor_y = 0;
}

int console_read_char(void) {
    if (!g_uart_ready) {
        return -1;
    }
    while (g_uart_ready) {
        int character = uart_try_get();
        if (character >= 0) {
            return character;
        }
        __asm__ volatile("wfe");
    }
    return -1;
}

bool console_has_input(void) {
    return g_uart_ready;
}

void console_write_u64(uint64_t value) {
    char digits[20] = {0};
    size_t count = 0;

    if (value == 0u) {
        console_write_char('0');
        return;
    }
    while (value > 0u && count < sizeof digits) {
        digits[count++] = (char)('0' + (char)(value % 10u));
        value /= 10u;
    }
    while (count > 0u) {
        console_write_char(digits[--count]);
    }
}
