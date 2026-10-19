#include "display_server.h"
#include "framebuffer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    TEST_FB_WIDTH = 640u,
    TEST_FB_HEIGHT = 480u
};

static uint32_t g_pixels[TEST_FB_WIDTH * TEST_FB_HEIGHT];
static int g_failures;

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

static void paint_probe(const AstraDisplayWindow *window, void *context) {
    (void)context;
    astra_display_window_fill(window, 0u, 0u, 12u, 12u, UINT32_C(0x123456));
    astra_display_window_fill(
        window,
        window->content_width - 1u,
        0u,
        UINT32_MAX,
        1u,
        UINT32_C(0xabcdef)
    );
    astra_display_window_text(
        window, 16u, 16u, "OK", 2u,
        UINT32_C(0xffffff), UINT32_C(0x123456)
    );
}

static void test_window_creation_focus_and_bounds(void) {
    AstraBootInfo info = make_boot_info();
    uint32_t terminal_id = 0u;
    uint32_t duplicate_id = 0u;
    uint32_t second_id = 0u;
    uint32_t third_id = 0u;
    uint32_t fourth_id = 0u;
    uint32_t rejected_id = 99u;
    const AstraDisplayWindow *focused = NULL;

    astra_framebuffer_init(&info);
    CHECK(astra_framebuffer_is_active());
    CHECK(astra_display_server_initialize());
    CHECK(astra_display_server_is_active());
    CHECK(astra_display_server_width() == TEST_FB_WIDTH);
    CHECK(astra_display_server_height() == TEST_FB_HEIGHT);
    CHECK(astra_display_server_window_count() == 0u);

    CHECK(astra_display_server_open_window(
        "Terminal", 400u, 240u, paint_probe, NULL, &terminal_id));
    CHECK(terminal_id != 0u);
    CHECK(astra_display_server_window_count() == 1u);
    focused = astra_display_server_focused_window();
    CHECK(focused != NULL);
    CHECK(focused != NULL && focused->id == terminal_id && focused->focused);

    CHECK(astra_display_server_open_window(
        "Terminal", 400u, 240u, paint_probe, NULL, &duplicate_id));
    CHECK(duplicate_id == terminal_id);
    CHECK(astra_display_server_window_count() == 1u);

    CHECK(astra_display_server_open_window(
        "System", 200u, 140u, paint_probe, NULL, &second_id));
    CHECK(astra_display_server_open_window(
        "Files", 200u, 140u, paint_probe, NULL, &third_id));
    CHECK(astra_display_server_open_window(
        "Logs", 200u, 140u, paint_probe, NULL, &fourth_id));
    CHECK(astra_display_server_window_count() == ASTRA_DISPLAY_MAX_WINDOWS);
    CHECK(!astra_display_server_open_window(
        "Overflow", 200u, 140u, paint_probe, NULL, &rejected_id));
    CHECK(rejected_id == 0u);

    CHECK(astra_display_server_focus_next());
    focused = astra_display_server_focused_window();
    CHECK(focused != NULL);
    CHECK(focused != NULL && focused->id == terminal_id);

    astra_display_server_render();
    focused = astra_display_server_focused_window();
    CHECK(focused != NULL);
    if (focused != NULL) {
        size_t content_offset =
            ((size_t)(focused->y + 28u) * (size_t)TEST_FB_WIDTH) +
            (size_t)(focused->x + 4u);
        size_t edge_offset =
            ((size_t)(focused->y + 28u) * (size_t)TEST_FB_WIDTH) +
            (size_t)(focused->x + 4u + focused->content_width - 1u);
        CHECK(g_pixels[content_offset] == UINT32_C(0xff123456));
        CHECK(g_pixels[edge_offset] == UINT32_C(0xffabcdef));
    }

    CHECK(!astra_display_server_close_window(UINT32_C(0xffffffff)));
    CHECK(astra_display_server_close_window(second_id));
    CHECK(astra_display_server_window_count() == 3u);
    CHECK(astra_display_server_focused_window() != NULL);
    CHECK(fourth_id != 0u && third_id != 0u);
    astra_display_server_shutdown();
    CHECK(!astra_display_server_is_active());
    CHECK(astra_framebuffer_is_active());

    astra_framebuffer_put_byte((uint8_t)'A');
    CHECK(g_pixels[(size_t)TEST_FB_WIDTH + 2u] == UINT32_C(0xfff0f4f8));
}

static void test_too_small_framebuffer_rejected(void) {
    uint32_t small_pixels[160u * 120u] = {0};
    AstraBootInfo info = {0};

    info.magic = ASTRA_BOOT_INFO_MAGIC;
    info.version = ASTRA_BOOT_INFO_VERSION;
    info.framebuffer_base = (uint64_t)(uintptr_t)small_pixels;
    info.framebuffer_size = (uint64_t)sizeof small_pixels;
    info.framebuffer_width = 160u;
    info.framebuffer_height = 120u;
    info.framebuffer_pixels_per_scanline = 160u;
    info.framebuffer_pixel_format = 1u;
    astra_framebuffer_init(&info);
    CHECK(astra_framebuffer_is_active());
    CHECK(!astra_display_server_initialize());
    CHECK(!astra_display_server_is_active());
}

int main(void) {
    test_window_creation_focus_and_bounds();
    test_too_small_framebuffer_rejected();

    if (g_failures != 0) {
        (void)fprintf(stderr, "%d AstraOS display-server test(s) failed\n",
            g_failures);
        return EXIT_FAILURE;
    }
    (void)puts("All AstraOS display-server tests passed.");
    return EXIT_SUCCESS;
}
