#include "desktop.h"

#include "display_server.h"
#include "framebuffer.h"
#include "input.h"
#include "uart.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    RGB_BACKGROUND = 0xf1f5f9u,
    RGB_TEXT = 0x1b2a41u,
    RGB_MUTED = 0x53647bu,
    RGB_ACCENT = 0x2e6bb2u,
    RGB_CARD = 0xe3ebf5u
};

static uint32_t g_welcome_window = 0u;
static uint32_t g_system_window = 0u;

static void render_welcome(uint32_t width, uint32_t height, void *context) {
    (void)context;
    astra_display_draw_rect(0u, 0u, width, height, RGB_BACKGROUND);
    astra_display_draw_text(16u, 16u, "WELCOME TO ASTRAOS", RGB_TEXT, RGB_BACKGROUND);
    astra_display_draw_text(16u, 44u, "An experimental AArch64 operating system",
        RGB_MUTED, RGB_BACKGROUND);
    astra_display_draw_rect(16u, 72u, width > 32u ? width - 32u : width, 3u, RGB_ACCENT);
    astra_display_draw_text(16u, 92u, "The graphical framebuffer compositor is active.",
        RGB_TEXT, RGB_BACKGROUND);
    astra_display_draw_text(16u, 116u, "Windows are retained and repainted in z-order.",
        RGB_TEXT, RGB_BACKGROUND);
    astra_display_draw_text(16u, 140u, "Press 1 for Welcome, 2 for System Information.",
        RGB_TEXT, RGB_BACKGROUND);
    astra_display_draw_text(16u, 164u, "Press T for the kernel terminal or Q to leave.",
        RGB_TEXT, RGB_BACKGROUND);
    astra_display_draw_rect(16u, 202u, width > 32u ? width - 32u : width,
        48u, RGB_CARD);
    astra_display_draw_text(28u, 214u, "AstraOS alpha | UEFI GOP | ARM64",
        RGB_TEXT, RGB_CARD);
}

static void render_system(uint32_t width, uint32_t height, void *context) {
    (void)context;
    astra_display_draw_rect(0u, 0u, width, height, RGB_BACKGROUND);
    astra_display_draw_text(14u, 14u, "SYSTEM INFORMATION", RGB_TEXT, RGB_BACKGROUND);
    astra_display_draw_rect(14u, 38u, width > 28u ? width - 28u : width, 2u, RGB_ACCENT);
    astra_display_draw_text(14u, 54u, "Architecture: AArch64", RGB_TEXT, RGB_BACKGROUND);
    astra_display_draw_text(14u, 78u, "Display: UEFI GOP framebuffer", RGB_TEXT, RGB_BACKGROUND);
    astra_display_draw_text(14u, 102u, "Compositor: software, kernel-owned", RGB_TEXT, RGB_BACKGROUND);
    astra_display_draw_text(14u, 126u, "Window limit: 6", RGB_TEXT, RGB_BACKGROUND);
    astra_display_draw_text(14u, 150u, "Input: firmware keyboard or serial", RGB_TEXT, RGB_BACKGROUND);
    astra_display_draw_text(14u, 174u, "T: terminal  |  Q: close desktop", RGB_MUTED, RGB_BACKGROUND);
    if (height > 212u) {
        astra_display_draw_text(14u, 198u, "No mouse or userspace apps yet.",
            RGB_MUTED, RGB_BACKGROUND);
    }
}

void astra_desktop_run(void) {
    uint32_t screen_width = 0u;
    uint32_t screen_height = 0u;
    uint32_t welcome_width = 0u;
    uint32_t welcome_height = 0u;
    uint32_t system_width = 0u;
    uint32_t system_height = 0u;
    uint8_t key = 0u;

    g_welcome_window = 0u;
    g_system_window = 0u;
    if (!astra_display_server_initialize()) {
        astra_uart_puts("[desktop] unavailable: GOP framebuffer is too small or missing.\n");
        return;
    }

    screen_width = astra_display_server_width();
    screen_height = astra_display_server_height();
    welcome_width = screen_width > 640u ? 560u : screen_width - 40u;
    welcome_height = screen_height > 500u ? 340u : screen_height - 90u;
    system_width = screen_width > 620u ? 380u : screen_width - 40u;
    system_height = screen_height > 500u ? 220u : screen_height - 90u;

    if (!astra_display_window_create(
            "Welcome to AstraOS",
            (screen_width - welcome_width) / 2u,
            (screen_height - welcome_height) / 2u,
            welcome_width,
            welcome_height,
            render_welcome,
            NULL,
            &g_welcome_window
        ) ||
        !astra_display_window_create(
            "System Information",
            screen_width - system_width - 20u,
            42u,
            system_width,
            system_height,
            render_system,
            NULL,
            &g_system_window
        )) {
        astra_uart_puts("[desktop] unable to create the initial windows.\n");
        astra_display_server_shutdown();
        return;
    }

    astra_display_server_present();
    astra_uart_puts(
        "\nAstraOS desktop session started. Keys: 1 Welcome, 2 System, T Terminal, Q Exit.\n"
    );

    for (;;) {
        if (!astra_input_read_byte(&key)) {
            astra_uart_puts("[desktop] input stopped; returning to kernel shell.\n");
            break;
        }
        if (key == (uint8_t)'q' || key == (uint8_t)'Q') {
            astra_uart_puts("[desktop] closing desktop and returning to kernel shell.\n");
            break;
        }
        if (key == (uint8_t)'t' || key == (uint8_t)'T') {
            astra_uart_puts("[desktop] switching to kernel terminal.\n");
            break;
        }
        if (key == (uint8_t)'1') {
            (void)astra_display_window_set_visible(g_welcome_window, true);
            (void)astra_display_window_raise(g_welcome_window);
            astra_display_server_present();
        } else if (key == (uint8_t)'2') {
            (void)astra_display_window_set_visible(g_system_window, true);
            (void)astra_display_window_raise(g_system_window);
            astra_display_server_present();
        } else if (key == (uint8_t)'h' || key == (uint8_t)'H') {
            (void)astra_display_window_set_visible(g_welcome_window, true);
            (void)astra_display_window_set_visible(g_system_window, true);
            (void)astra_display_window_raise(g_welcome_window);
            astra_display_server_present();
        } else if (key == (uint8_t)'r' || key == (uint8_t)'R') {
            astra_display_server_present();
        }
    }

    astra_display_server_shutdown();
}
