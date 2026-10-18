#include <stdint.h>

#include "limine.h"
#include "astra/vfs.h"
#include "console.h"
#include "shell.h"

#if !defined(__GNUC__) && !defined(__clang__)
#error "AstraOS currently requires a GCC- or Clang-compatible freestanding toolchain."
#endif

#define ASTRA_SECTION(name) __attribute__((used, section(name), aligned(8)))

ASTRA_SECTION(".limine_requests_start")
static volatile uint64_t g_limine_requests_start[4] = LIMINE_REQUESTS_START_MARKER;

ASTRA_SECTION(".limine_requests")
static volatile uint64_t g_limine_base_revision[3] = LIMINE_BASE_REVISION(0);

ASTRA_SECTION(".limine_requests")
static volatile struct limine_hhdm_request g_hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

ASTRA_SECTION(".limine_requests")
static volatile struct limine_framebuffer_request g_framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

ASTRA_SECTION(".limine_requests")
static volatile struct limine_dtb_request g_dtb_request = {
    .id = LIMINE_DTB_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

ASTRA_SECTION(".limine_requests")
static volatile struct limine_memmap_request g_memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

ASTRA_SECTION(".limine_requests_end")
static volatile uint64_t g_limine_requests_end[2] = LIMINE_REQUESTS_END_MARKER;

__attribute__((noreturn)) void kernel_main(void);

__attribute__((noreturn))
void kernel_main(void) {
    struct limine_hhdm_response *hhdm = g_hhdm_request.response;
    struct limine_framebuffer_response *framebuffer = g_framebuffer_request.response;
    struct limine_dtb_response *dtb = g_dtb_request.response;
    struct limine_memmap_response *memory_map = g_memmap_request.response;
    uint64_t hhdm_offset = hhdm != NULL ? hhdm->offset : 0u;

    (void)g_limine_base_revision;
    (void)g_limine_requests_start;
    (void)g_limine_requests_end;
    console_init(framebuffer, dtb, hhdm_offset);
    vfs_init();

    console_write("\n");
    console_write("  /\\    AstraOS 0.2.0\n");
    console_write(" /  \\   Native AArch64 kernel\n");
    console_write("/____\\  Limine handoff | RAM VFS | UID/mode checks\n\n");
    console_write("Kernel entry reached. Firmware boot services are no longer used.\n");
    if (memory_map != NULL) {
        console_write("Limine memory-map entries: ");
        console_write_u64(memory_map->entry_count);
        console_write("\n");
    } else {
        console_write("Warning: Limine did not return a memory map.\n");
    }
    if (framebuffer != NULL && framebuffer->framebuffer_count > 0u) {
        console_write("Framebuffer: detected\n");
    } else {
        console_write("Framebuffer: unavailable\n");
    }

    if (console_has_input()) {
        console_write("PL011 serial console: ready\n");
        console_write("Type 'help' for commands. All file changes are temporary.\n\n");
        shell_run();
        console_write("Kernel halted by shell exit.\n");
    } else {
        console_write("No device-tree PL011 console was found.\n");
        console_write("This build has no USB keyboard driver yet; it is in read-only display mode.\n");
    }

    for (;;) {
        __asm__ volatile("wfe");
    }
}
