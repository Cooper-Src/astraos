#include "framebuffer.h"

#include <stddef.h>
#include <stdint.h>

enum {
    FB_CELL_WIDTH = 8u,
    FB_CELL_HEIGHT = 16u,
    FB_FONT_WIDTH = 5u,
    FB_FONT_HEIGHT = 7u,
    FB_PIXEL_RGB_RESERVED = 0u,
    FB_PIXEL_BGR_RESERVED = 1u,
    FB_PIXEL_BITMASK = 2u,
    FB_PIXEL_BLT_ONLY = 3u,
    FB_MAX_DIMENSION = 8192u,
    FB_TAB_CELLS = 4u
};

typedef struct AstraFramebufferState {
    volatile uint32_t *pixels;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint8_t escape_state;
    bool active;
    bool console_enabled;
} AstraFramebufferState;

static AstraFramebufferState g_fb;

static const uint8_t g_letter_glyphs[26][FB_FONT_HEIGHT] = {
    {0x0eu, 0x11u, 0x11u, 0x1fu, 0x11u, 0x11u, 0x11u},
    {0x1eu, 0x11u, 0x11u, 0x1eu, 0x11u, 0x11u, 0x1eu},
    {0x0eu, 0x11u, 0x10u, 0x10u, 0x10u, 0x11u, 0x0eu},
    {0x1eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1eu},
    {0x1fu, 0x10u, 0x10u, 0x1eu, 0x10u, 0x10u, 0x1fu},
    {0x1fu, 0x10u, 0x10u, 0x1eu, 0x10u, 0x10u, 0x10u},
    {0x0eu, 0x11u, 0x10u, 0x17u, 0x11u, 0x11u, 0x0fu},
    {0x11u, 0x11u, 0x11u, 0x1fu, 0x11u, 0x11u, 0x11u},
    {0x0eu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x0eu},
    {0x07u, 0x02u, 0x02u, 0x02u, 0x12u, 0x12u, 0x0cu},
    {0x11u, 0x12u, 0x14u, 0x18u, 0x14u, 0x12u, 0x11u},
    {0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x1fu},
    {0x11u, 0x1bu, 0x15u, 0x15u, 0x11u, 0x11u, 0x11u},
    {0x11u, 0x19u, 0x15u, 0x13u, 0x11u, 0x11u, 0x11u},
    {0x0eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0eu},
    {0x1eu, 0x11u, 0x11u, 0x1eu, 0x10u, 0x10u, 0x10u},
    {0x0eu, 0x11u, 0x11u, 0x11u, 0x15u, 0x12u, 0x0du},
    {0x1eu, 0x11u, 0x11u, 0x1eu, 0x14u, 0x12u, 0x11u},
    {0x0fu, 0x10u, 0x10u, 0x0eu, 0x01u, 0x01u, 0x1eu},
    {0x1fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u},
    {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0eu},
    {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0au, 0x04u},
    {0x11u, 0x11u, 0x11u, 0x15u, 0x15u, 0x15u, 0x0au},
    {0x11u, 0x11u, 0x0au, 0x04u, 0x0au, 0x11u, 0x11u},
    {0x11u, 0x11u, 0x0au, 0x04u, 0x04u, 0x04u, 0x04u},
    {0x1fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x1fu}
};

static const uint8_t g_digit_glyphs[10][FB_FONT_HEIGHT] = {
    {0x0eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0eu},
    {0x04u, 0x0cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0eu},
    {0x0eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1fu},
    {0x1eu, 0x01u, 0x01u, 0x0eu, 0x01u, 0x01u, 0x1eu},
    {0x02u, 0x06u, 0x0au, 0x12u, 0x1fu, 0x02u, 0x02u},
    {0x1fu, 0x10u, 0x10u, 0x1eu, 0x01u, 0x01u, 0x1eu},
    {0x0eu, 0x10u, 0x10u, 0x1eu, 0x11u, 0x11u, 0x0eu},
    {0x1fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u},
    {0x0eu, 0x11u, 0x11u, 0x0eu, 0x11u, 0x11u, 0x0eu},
    {0x0eu, 0x11u, 0x11u, 0x0fu, 0x01u, 0x01u, 0x0eu}
};

static uint8_t special_glyph_row(uint8_t character, uint32_t row) {
    switch (character) {
        case ' ':
            return 0u;
        case '!':
            return row == 1u || row == 2u || row == 3u ||
                row == 4u || row == 6u ? 0x04u : 0u;
        case '#':
            return row == 0u || row == 1u || row == 3u || row == 4u ||
                row == 6u ? 0x0au : (row == 2u || row == 5u ? 0x1fu : 0u);
        case '$': {
            static const uint8_t rows[FB_FONT_HEIGHT] = {
                0x04u, 0x0fu, 0x14u, 0x0eu, 0x05u, 0x1eu, 0x04u
            };
            return rows[row];
        }
        case '%': {
            static const uint8_t rows[FB_FONT_HEIGHT] = {
                0x19u, 0x19u, 0x02u, 0x04u, 0x08u, 0x13u, 0x13u
            };
            return rows[row];
        }
        case '&': {
            static const uint8_t rows[FB_FONT_HEIGHT] = {
                0x0cu, 0x12u, 0x14u, 0x08u, 0x15u, 0x12u, 0x0du
            };
            return rows[row];
        }
        case 0x27u:
            return row < 2u ? 0x04u : 0u;
        case '(':
            return row == 0u || row == 6u ? 0x02u :
                (row == 1u || row == 5u ? 0x04u : 0x08u);
        case ')':
            return row == 0u || row == 6u ? 0x08u :
                (row == 1u || row == 5u ? 0x04u : 0x02u);
        case '*':
            return row == 1u ? 0x15u :
                (row == 2u || row == 4u ? 0x0eu : 0x04u);
        case '+':
            return row == 3u ? 0x1fu :
                (row == 1u || row == 2u || row == 4u || row == 5u ? 0x04u : 0u);
        case ',':
            return row == 5u ? 0x04u : (row == 6u ? 0x08u : 0u);
        case '-':
            return row == 3u ? 0x1fu : 0u;
        case '.':
            return row == 6u ? 0x04u : 0u;
        case '/': {
            static const uint8_t rows[FB_FONT_HEIGHT] = {
                0x01u, 0x02u, 0x02u, 0x04u, 0x08u, 0x08u, 0x10u
            };
            return rows[row];
        }
        case ':':
            return row == 2u || row == 5u ? 0x04u : 0u;
        case ';':
            return row == 2u ? 0x04u : (row == 5u ? 0x04u : (row == 6u ? 0x08u : 0u));
        case '<':
            return row == 0u || row == 6u ? 0x10u :
                (row == 1u || row == 5u ? 0x08u :
                (row == 2u || row == 4u ? 0x04u : 0x02u));
        case '=':
            return row == 2u || row == 4u ? 0x1fu : 0u;
        case '>':
            return row == 0u || row == 6u ? 0x01u :
                (row == 1u || row == 5u ? 0x02u :
                (row == 2u || row == 4u ? 0x04u : 0x08u));
        case '?': {
            static const uint8_t rows[FB_FONT_HEIGHT] = {
                0x0eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x00u, 0x04u
            };
            return rows[row];
        }
        case '@': {
            static const uint8_t rows[FB_FONT_HEIGHT] = {
                0x0eu, 0x11u, 0x17u, 0x15u, 0x17u, 0x10u, 0x0eu
            };
            return rows[row];
        }
        case '[':
            return row == 0u || row == 6u ? 0x0eu : 0x08u;
        case 0x5cu:
            return row == 0u || row == 6u ? 0x10u :
                (row == 1u || row == 5u ? 0x08u :
                (row == 2u || row == 4u ? 0x04u : 0x02u));
        case ']':
            return row == 0u || row == 6u ? 0x0eu : 0x02u;
        case '^':
            return row == 0u ? 0x04u : (row == 1u ? 0x0au : 0u);
        case '_':
            return row == 6u ? 0x1fu : 0u;
        case 0x60u:
            return row == 0u ? 0x08u : 0u;
        case '{':
            return row == 0u || row == 6u ? 0x06u :
                (row == 1u || row == 5u ? 0x04u : 0x0cu);
        case '|':
            return 0x04u;
        case '}':
            return row == 0u || row == 6u ? 0x0cu :
                (row == 1u || row == 5u ? 0x04u : 0x06u);
        case '~':
            return row == 2u ? 0x0du : (row == 3u ? 0x12u : 0u);
        default:
            return row == 0u || row == 6u ? 0x0eu :
                (row == 1u || row == 5u ? 0x11u :
                (row == 2u || row == 4u ? 0x01u : 0x02u));
    }
}

static uint8_t glyph_row(uint8_t character, uint32_t row) {
    if (character >= (uint8_t)'a' && character <= (uint8_t)'z') {
        character = (uint8_t)(character - ((uint8_t)'a' - (uint8_t)'A'));
    }
    if (character >= (uint8_t)'A' && character <= (uint8_t)'Z') {
        return g_letter_glyphs[(size_t)(character - (uint8_t)'A')][row];
    }
    if (character >= (uint8_t)'0' && character <= (uint8_t)'9') {
        return g_digit_glyphs[(size_t)(character - (uint8_t)'0')][row];
    }
    return special_glyph_row(character, row);
}

static uint32_t mask_component(uint8_t value, uint32_t mask) {
    uint32_t shift = 0;
    uint32_t normalized = mask;
    uint64_t scaled = 0;

    if (mask == 0u) {
        return 0u;
    }
    while ((normalized & 1u) == 0u) {
        normalized >>= 1;
        shift++;
    }
    scaled = (((uint64_t)value * (uint64_t)normalized) + 127u) / 255u;
    return (uint32_t)((scaled << shift) & (uint64_t)mask);
}

static bool mask_is_contiguous(uint32_t mask) {
    if (mask == 0u) {
        return false;
    }
    while ((mask & 1u) == 0u) {
        mask >>= 1u;
    }
    return (mask & (mask + 1u)) == 0u;
}

static uint32_t color_pixel(uint8_t red, uint8_t green, uint8_t blue) {
    const uint8_t reserved = 255u;

    if (g_fb.format == FB_PIXEL_RGB_RESERVED) {
        return (uint32_t)red | ((uint32_t)green << 8) |
            ((uint32_t)blue << 16) | ((uint32_t)reserved << 24);
    }
    if (g_fb.format == FB_PIXEL_BGR_RESERVED) {
        return (uint32_t)blue | ((uint32_t)green << 8) |
            ((uint32_t)red << 16) | ((uint32_t)reserved << 24);
    }
    return mask_component(red, g_fb.red_mask) |
        mask_component(green, g_fb.green_mask) |
        mask_component(blue, g_fb.blue_mask) |
        mask_component(reserved, g_fb.reserved_mask);
}

static void fill_rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t color
) {
    if (!g_fb.active || x >= g_fb.width || y >= g_fb.height ||
        width > g_fb.width - x || height > g_fb.height - y) {
        return;
    }

    for (uint32_t row = 0; row < height; row++) {
        size_t offset = (size_t)(y + row) * (size_t)g_fb.stride + (size_t)x;
        for (uint32_t column = 0; column < width; column++) {
            g_fb.pixels[offset + (size_t)column] = color;
        }
    }
}

bool astra_framebuffer_is_active(void) {
    return g_fb.active;
}

void astra_framebuffer_clear(void) {
    if (!g_fb.active) {
        return;
    }
    fill_rect(0u, 0u, g_fb.width, g_fb.height, color_pixel(8u, 14u, 24u));
    g_fb.cursor_x = 0u;
    g_fb.cursor_y = 0u;
    g_fb.escape_state = 0u;
}

static void scroll_up(void) {
    uint32_t visible_height = 0;

    if (!g_fb.active) {
        return;
    }
    if (g_fb.height <= FB_CELL_HEIGHT) {
        astra_framebuffer_clear();
        return;
    }

    visible_height = g_fb.height - FB_CELL_HEIGHT;
    for (uint32_t y = 0; y < visible_height; y++) {
        size_t destination = (size_t)y * (size_t)g_fb.stride;
        size_t source = (size_t)(y + FB_CELL_HEIGHT) * (size_t)g_fb.stride;

        for (uint32_t x = 0; x < g_fb.width; x++) {
            g_fb.pixels[destination + (size_t)x] =
                g_fb.pixels[source + (size_t)x];
        }
    }

    fill_rect(
        0u,
        visible_height,
        g_fb.width,
        FB_CELL_HEIGHT,
        color_pixel(8u, 14u, 24u)
    );
    g_fb.cursor_y = visible_height;
}

static void advance_line(void) {
    g_fb.cursor_x = 0u;
    if (g_fb.cursor_y + FB_CELL_HEIGHT >= g_fb.height) {
        scroll_up();
    } else {
        g_fb.cursor_y += FB_CELL_HEIGHT;
    }
}

static void draw_cell(uint8_t character, bool advance) {
    uint32_t background = 0;
    uint32_t foreground = 0;

    if (!g_fb.active) {
        return;
    }
    if (g_fb.cursor_x + FB_CELL_WIDTH > g_fb.width) {
        advance_line();
    }

    background = color_pixel(8u, 14u, 24u);
    foreground = color_pixel(240u, 244u, 248u);
    fill_rect(g_fb.cursor_x, g_fb.cursor_y, FB_CELL_WIDTH, FB_CELL_HEIGHT, background);

    for (uint32_t row = 0; row < FB_FONT_HEIGHT; row++) {
        uint8_t bits = glyph_row(character, row);
        for (uint32_t column = 0; column < FB_FONT_WIDTH; column++) {
            uint8_t bit = (uint8_t)(1u << (FB_FONT_WIDTH - 1u - column));
            if ((bits & bit) != 0u) {
                uint32_t pixel_x = g_fb.cursor_x + 1u + column;
                uint32_t pixel_y = g_fb.cursor_y + 1u + (row * 2u);
                fill_rect(pixel_x, pixel_y, 1u, 2u, foreground);
            }
        }
    }

    if (advance) {
        g_fb.cursor_x += FB_CELL_WIDTH;
    }
}

static void handle_escape(uint8_t value) {
    if (g_fb.escape_state == 1u) {
        g_fb.escape_state = value == (uint8_t)'[' ? 2u : 0u;
        return;
    }
    if (g_fb.escape_state == 2u) {
        if (value == (uint8_t)'2') {
            g_fb.escape_state = 3u;
        } else if (value == (uint8_t)'H') {
            g_fb.cursor_x = 0u;
            g_fb.cursor_y = 0u;
            g_fb.escape_state = 0u;
        } else {
            g_fb.escape_state = 0u;
        }
        return;
    }
    if (g_fb.escape_state == 3u) {
        if (value == (uint8_t)'J') {
            astra_framebuffer_clear();
        }
        g_fb.escape_state = 0u;
    }
}

void astra_framebuffer_put_byte(uint8_t value) {
    if (!g_fb.active || !g_fb.console_enabled) {
        return;
    }
    if (g_fb.escape_state != 0u) {
        handle_escape(value);
        return;
    }
    if (value == 27u) {
        g_fb.escape_state = 1u;
        return;
    }
    if (value == 13u) {
        g_fb.cursor_x = 0u;
        return;
    }
    if (value == 10u) {
        advance_line();
        return;
    }
    if (value == 8u || value == 127u) {
        if (g_fb.cursor_x >= FB_CELL_WIDTH) {
            g_fb.cursor_x -= FB_CELL_WIDTH;
            draw_cell((uint8_t)' ', false);
        }
        return;
    }
    if (value == 9u) {
        uint32_t target = ((g_fb.cursor_x / FB_CELL_WIDTH / FB_TAB_CELLS) + 1u) *
            FB_TAB_CELLS * FB_CELL_WIDTH;
        while (g_fb.cursor_x < target) {
            draw_cell((uint8_t)' ', true);
        }
        return;
    }
    if (value >= 0x20u && value <= 0x7eu) {
        draw_cell(value, true);
    }
}

void astra_framebuffer_init(const AstraBootInfo *boot_info) {
    uint64_t pixel_count = 0;
    uint64_t required_bytes = 0;
    uint32_t combined_mask = 0;

    g_fb.pixels = NULL;
    g_fb.width = 0u;
    g_fb.height = 0u;
    g_fb.stride = 0u;
    g_fb.format = FB_PIXEL_BLT_ONLY;
    g_fb.red_mask = 0u;
    g_fb.green_mask = 0u;
    g_fb.blue_mask = 0u;
    g_fb.reserved_mask = 0u;
    g_fb.cursor_x = 0u;
    g_fb.cursor_y = 0u;
    g_fb.escape_state = 0u;
    g_fb.active = false;
    g_fb.console_enabled = true;

    if (boot_info == NULL || boot_info->magic != ASTRA_BOOT_INFO_MAGIC ||
        boot_info->version != ASTRA_BOOT_INFO_VERSION ||
        boot_info->framebuffer_base == 0u ||
        boot_info->framebuffer_base > (uint64_t)UINTPTR_MAX ||
        boot_info->framebuffer_size == 0u ||
        boot_info->framebuffer_width < FB_CELL_WIDTH ||
        boot_info->framebuffer_height < FB_CELL_HEIGHT ||
        boot_info->framebuffer_width > FB_MAX_DIMENSION ||
        boot_info->framebuffer_height > FB_MAX_DIMENSION ||
        boot_info->framebuffer_pixels_per_scanline < boot_info->framebuffer_width ||
        boot_info->framebuffer_pixels_per_scanline > FB_MAX_DIMENSION ||
        boot_info->framebuffer_pixel_format > FB_PIXEL_BITMASK) {
        return;
    }

    pixel_count = (uint64_t)boot_info->framebuffer_pixels_per_scanline *
        (uint64_t)boot_info->framebuffer_height;
    if (pixel_count > UINT64_MAX / sizeof(uint32_t)) {
        return;
    }
    required_bytes = pixel_count * sizeof(uint32_t);
    if (required_bytes > boot_info->framebuffer_size ||
        boot_info->framebuffer_base > UINT64_MAX - required_bytes ||
        required_bytes > (uint64_t)UINTPTR_MAX - boot_info->framebuffer_base) {
        return;
    }

    if (boot_info->framebuffer_pixel_format == FB_PIXEL_BITMASK) {
        combined_mask = boot_info->framebuffer_red_mask |
            boot_info->framebuffer_green_mask |
            boot_info->framebuffer_blue_mask |
            boot_info->framebuffer_reserved_mask;
        if (combined_mask == 0u ||
            !mask_is_contiguous(boot_info->framebuffer_red_mask) ||
            !mask_is_contiguous(boot_info->framebuffer_green_mask) ||
            !mask_is_contiguous(boot_info->framebuffer_blue_mask) ||
            (boot_info->framebuffer_reserved_mask != 0u &&
                !mask_is_contiguous(boot_info->framebuffer_reserved_mask)) ||
            (boot_info->framebuffer_red_mask & boot_info->framebuffer_green_mask) != 0u ||
            (boot_info->framebuffer_red_mask & boot_info->framebuffer_blue_mask) != 0u ||
            (boot_info->framebuffer_red_mask & boot_info->framebuffer_reserved_mask) != 0u ||
            (boot_info->framebuffer_green_mask & boot_info->framebuffer_blue_mask) != 0u ||
            (boot_info->framebuffer_green_mask & boot_info->framebuffer_reserved_mask) != 0u ||
            (boot_info->framebuffer_blue_mask & boot_info->framebuffer_reserved_mask) != 0u) {
            return;
        }
    }

    g_fb.pixels = (volatile uint32_t *)(uintptr_t)boot_info->framebuffer_base;
    g_fb.width = boot_info->framebuffer_width;
    g_fb.height = boot_info->framebuffer_height;
    g_fb.stride = boot_info->framebuffer_pixels_per_scanline;
    g_fb.format = boot_info->framebuffer_pixel_format;
    g_fb.red_mask = boot_info->framebuffer_red_mask;
    g_fb.green_mask = boot_info->framebuffer_green_mask;
    g_fb.blue_mask = boot_info->framebuffer_blue_mask;
    g_fb.reserved_mask = boot_info->framebuffer_reserved_mask;
    g_fb.active = true;
    astra_framebuffer_clear();
}

bool astra_framebuffer_get_dimensions(uint32_t *width_out, uint32_t *height_out) {
    if (!g_fb.active || width_out == NULL || height_out == NULL) {
        return false;
    }
    *width_out = g_fb.width;
    *height_out = g_fb.height;
    return true;
}

void astra_framebuffer_set_console_enabled(bool enabled) {
    g_fb.console_enabled = enabled;
}

void astra_framebuffer_fill_rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t rgb24
) {
    if (!g_fb.active || x >= g_fb.width || y >= g_fb.height ||
        width == 0u || height == 0u) {
        return;
    }
    if (width > g_fb.width - x) {
        width = g_fb.width - x;
    }
    if (height > g_fb.height - y) {
        height = g_fb.height - y;
    }
    fill_rect(
        x, y, width, height,
        color_pixel(
            (uint8_t)((rgb24 >> 16u) & UINT32_C(0xff)),
            (uint8_t)((rgb24 >> 8u) & UINT32_C(0xff)),
            (uint8_t)(rgb24 & UINT32_C(0xff))
        )
    );
}

void astra_framebuffer_draw_text(
    uint32_t x,
    uint32_t y,
    const char *text,
    size_t length,
    uint32_t rgb24
) {
    uint32_t color = 0u;
    size_t max_chars = 0u;

    if (!g_fb.active || text == NULL || x >= g_fb.width || y >= g_fb.height ||
        length == 0u) {
        return;
    }
    max_chars = (size_t)((g_fb.width - x) / FB_CELL_WIDTH);
    if (length > max_chars) {
        length = max_chars;
    }
    color = color_pixel(
        (uint8_t)((rgb24 >> 16u) & UINT32_C(0xff)),
        (uint8_t)((rgb24 >> 8u) & UINT32_C(0xff)),
        (uint8_t)(rgb24 & UINT32_C(0xff))
    );

    for (size_t index = 0u; index < length; index++) {
        uint8_t character = (uint8_t)text[index];
        uint32_t cell_x = x + ((uint32_t)index * FB_CELL_WIDTH);

        if (character == (uint8_t)'\n' || character == (uint8_t)'\r') {
            break;
        }
        if (character < 0x20u || character > 0x7eu) {
            character = (uint8_t)'?';
        }
        for (uint32_t row = 0u; row < FB_FONT_HEIGHT; row++) {
            uint8_t bits = glyph_row(character, row);
            uint32_t pixel_y = y + 1u + (row * 2u);

            if (pixel_y >= g_fb.height || g_fb.height - pixel_y < 2u) {
                continue;
            }
            for (uint32_t column = 0u; column < FB_FONT_WIDTH; column++) {
                uint8_t bit = (uint8_t)(1u << (FB_FONT_WIDTH - 1u - column));
                uint32_t pixel_x = cell_x + 1u + column;
                if ((bits & bit) != 0u && pixel_x < g_fb.width) {
                    fill_rect(pixel_x, pixel_y, 1u, 2u, color);
                }
            }
        }
    }
}
