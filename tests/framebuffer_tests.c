#include "framebuffer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    TEST_FB_WIDTH = 64u,
    TEST_FB_HEIGHT = 48u
};

static uint32_t g_pixels[TEST_FB_WIDTH * TEST_FB_HEIGHT];
static int g_failures = 0;

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            (void)fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                __FILE__, __LINE__, #expression); \
            g_failures++; \
        } \
    } while (0)

static AstraBootInfo make_boot_info(void) {
    AstraBootInfo info = {0};

    info.magic = ASTRA_BOOT_INFO_MAGIC;
    info.version = ASTRA_BOOT_INFO_VERSION;
    info.framebuffer_base = (uint64_t)(uintptr_t)g_pixels;
    info.framebuffer_size = (uint64_t)sizeof g_pixels;
    info.framebuffer_width = TEST_FB_WIDTH;
    info.framebuffer_height = TEST_FB_HEIGHT;
    info.framebuffer_pixels_per_scanline = TEST_FB_WIDTH;
    info.framebuffer_pixel_format = 1u;
    return info;
}

static void clear_pixels(void) {
    for (size_t i = 0; i < (size_t)(TEST_FB_WIDTH * TEST_FB_HEIGHT); i++) {
        g_pixels[i] = 0;
    }
}

static void test_framebuffer_init_and_glyph_render(void) {
    AstraBootInfo info = {0};

    clear_pixels();
    info = make_boot_info();
    astra_framebuffer_init(&info);
    CHECK(astra_framebuffer_is_active());
    CHECK(g_pixels[0] == UINT32_C(0xff080e18));

    astra_framebuffer_put_byte((uint8_t)'A');
    CHECK(g_pixels[(size_t)TEST_FB_WIDTH + 2u] == UINT32_C(0xfff0f4f8));
    CHECK(g_pixels[(size_t)TEST_FB_WIDTH + 1u] == UINT32_C(0xff080e18));

    astra_framebuffer_put_byte((uint8_t)'\n');
    astra_framebuffer_put_byte((uint8_t)'0');
    CHECK(g_pixels[(size_t)TEST_FB_WIDTH * 17u + 2u] == UINT32_C(0xfff0f4f8));
}

static void test_clear_and_escape_sequences(void) {
    AstraBootInfo info = {0};

    clear_pixels();
    info = make_boot_info();
    astra_framebuffer_init(&info);
    astra_framebuffer_put_byte((uint8_t)'X');
    CHECK(g_pixels[(size_t)TEST_FB_WIDTH + 1u] == UINT32_C(0xfff0f4f8));

    astra_framebuffer_put_byte(27u);
    astra_framebuffer_put_byte((uint8_t)'[');
    astra_framebuffer_put_byte((uint8_t)'2');
    astra_framebuffer_put_byte((uint8_t)'J');
    CHECK(g_pixels[(size_t)TEST_FB_WIDTH + 1u] == UINT32_C(0xff080e18));

    astra_framebuffer_put_byte(27u);
    astra_framebuffer_put_byte((uint8_t)'[');
    astra_framebuffer_put_byte((uint8_t)'H');
    astra_framebuffer_put_byte((uint8_t)'A');
    CHECK(g_pixels[(size_t)TEST_FB_WIDTH + 2u] == UINT32_C(0xfff0f4f8));
}

static void test_invalid_metadata_disables_framebuffer(void) {
    AstraBootInfo info = {0};

    info = make_boot_info();
    info.framebuffer_base = 0;
    astra_framebuffer_init(&info);
    CHECK(!astra_framebuffer_is_active());

    info = make_boot_info();
    info.framebuffer_width = TEST_FB_WIDTH + 1u;
    info.framebuffer_pixels_per_scanline = TEST_FB_WIDTH;
    astra_framebuffer_init(&info);
    CHECK(!astra_framebuffer_is_active());

    info = make_boot_info();
    info.framebuffer_size = 4u;
    astra_framebuffer_init(&info);
    CHECK(!astra_framebuffer_is_active());

    info = make_boot_info();
    info.framebuffer_base = UINT64_MAX - 4u;
    astra_framebuffer_init(&info);
    CHECK(!astra_framebuffer_is_active());

    info = make_boot_info();
    info.framebuffer_pixel_format = 3u;
    astra_framebuffer_init(&info);
    CHECK(!astra_framebuffer_is_active());

    info = make_boot_info();
    info.version = ASTRA_BOOT_INFO_VERSION - 1u;
    astra_framebuffer_init(&info);
    CHECK(!astra_framebuffer_is_active());
}

static void test_bitmask_format_validation(void) {
    AstraBootInfo info = {0};

    info = make_boot_info();
    info.framebuffer_pixel_format = 2u;
    info.framebuffer_red_mask = UINT32_C(0x00ff0000);
    info.framebuffer_green_mask = UINT32_C(0x0000ff00);
    info.framebuffer_blue_mask = UINT32_C(0x000000ff);
    info.framebuffer_reserved_mask = UINT32_C(0xff000000);
    astra_framebuffer_init(&info);
    CHECK(astra_framebuffer_is_active());

    info.framebuffer_green_mask = UINT32_C(0x00ff0000);
    astra_framebuffer_init(&info);
    CHECK(!astra_framebuffer_is_active());

    info = make_boot_info();
    info.framebuffer_pixel_format = 2u;
    info.framebuffer_red_mask = UINT32_C(0x00f50000);
    info.framebuffer_green_mask = UINT32_C(0x0000ff00);
    info.framebuffer_blue_mask = UINT32_C(0x000000ff);
    info.framebuffer_reserved_mask = UINT32_C(0xff000000);
    astra_framebuffer_init(&info);
    CHECK(!astra_framebuffer_is_active());
}

int main(void) {
    test_framebuffer_init_and_glyph_render();
    test_clear_and_escape_sequences();
    test_invalid_metadata_disables_framebuffer();
    test_bitmask_format_validation();

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d AstraOS framebuffer test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)puts("All AstraOS framebuffer tests passed.");
    return EXIT_SUCCESS;
}
