#include "display_server.h"
#include "framebuffer.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    TEST_WIDTH = 400u,
    TEST_HEIGHT = 300u,
    FIRST_X = 20u,
    FIRST_Y = 40u,
    FIRST_WIDTH = 240u,
    FIRST_HEIGHT = 160u,
    CONTENT_X = FIRST_X + 6u,
    CONTENT_Y = FIRST_Y + 28u,
    CONTENT_WIDTH = FIRST_WIDTH - 12u,
    CONTENT_HEIGHT = FIRST_HEIGHT - 34u
};

static uint32_t g_pixels[TEST_WIDTH * TEST_HEIGHT];
static int g_failures = 0;
static uint32_t g_render_count = 0u;

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
    info.framebuffer_width = TEST_WIDTH;
    info.framebuffer_height = TEST_HEIGHT;
    info.framebuffer_pixels_per_scanline = TEST_WIDTH;
    info.framebuffer_pixel_format = 1u;
    return info;
}

static void render_test_window(uint32_t width, uint32_t height, void *context) {
    (void)context;
    g_render_count++;
    astra_display_draw_rect(0u, 0u, width, height, 0x24aa88u);
    astra_display_draw_rect(width - 1u, height - 1u, 30u, 30u, 0xf04c68u);
    astra_display_draw_text(8u, 8u, "TEST WINDOW", 0x17243au, 0xe3ebf5u);
}

static void test_requires_working_framebuffer(void) {
    CHECK(!astra_display_server_initialize());
    CHECK(!astra_display_server_is_active());
}

static void test_window_composition_and_lifecycle(void) {
    AstraBootInfo info = make_boot_info();
    uint32_t first_id = 0u;
    uint32_t second_id = 0u;

    astra_framebuffer_init(&info);
    CHECK(astra_framebuffer_is_active());
    CHECK(astra_display_server_initialize());
    CHECK(astra_display_server_is_active());
    CHECK(astra_display_server_width() == TEST_WIDTH);
    CHECK(astra_display_server_height() == TEST_HEIGHT);

    CHECK(!astra_display_window_create("invalid", 380u, 0u, 240u, 160u,
        render_test_window, NULL, &first_id));
    CHECK(first_id == 0u);

    CHECK(astra_display_window_create("First", FIRST_X, FIRST_Y,
        FIRST_WIDTH, FIRST_HEIGHT, render_test_window, NULL, &first_id));
    CHECK(astra_display_window_create("Second", 180u, 80u, 200u, 150u,
        render_test_window, NULL, &second_id));
    CHECK(first_id != 0u && second_id != 0u && first_id != second_id);
    CHECK(astra_display_server_window_count() == 2u);

    astra_display_server_present();
    CHECK(g_render_count == 2u);
    CHECK(g_pixels[(size_t)CONTENT_Y * TEST_WIDTH + CONTENT_X] ==
        UINT32_C(0xff24aa88));
    CHECK(g_pixels[(size_t)(CONTENT_Y + CONTENT_HEIGHT - 1u) * TEST_WIDTH +
        CONTENT_X + CONTENT_WIDTH - 1u] == UINT32_C(0xfff04c68));
    CHECK(g_pixels[(size_t)CONTENT_Y * TEST_WIDTH +
        CONTENT_X + CONTENT_WIDTH] == UINT32_C(0xfff1f5f9));

    CHECK(astra_display_window_raise(first_id));
    astra_display_server_present();
    CHECK(g_render_count == 4u);
    CHECK(astra_display_window_close(second_id));
    astra_display_server_present();
    CHECK(g_render_count == 5u);
    CHECK(!astra_display_window_raise(second_id));
    CHECK(astra_display_window_set_visible(second_id, true));
    CHECK(astra_display_window_raise(second_id));
    astra_display_server_present();
    CHECK(g_render_count == 7u);

    astra_display_server_shutdown();
    CHECK(!astra_display_server_is_active());
    CHECK(astra_display_server_window_count() == 0u);
    CHECK(g_pixels[0] == UINT32_C(0xff080e18));
}

int main(void) {
    test_requires_working_framebuffer();
    test_window_composition_and_lifecycle();

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d AstraOS display-server test(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    (void)puts("All AstraOS display-server tests passed.");
    return EXIT_SUCCESS;
}
