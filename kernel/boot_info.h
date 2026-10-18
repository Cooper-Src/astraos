#ifndef ASTRAOS_BOOT_INFO_H
#define ASTRAOS_BOOT_INFO_H

#include <stdint.h>

#define ASTRA_BOOT_INFO_MAGIC UINT64_C(0x4153545241424f4f)
#define ASTRA_BOOT_INFO_VERSION UINT64_C(3)

typedef struct AstraBootInfo {
    uint64_t magic;
    uint64_t version;
    const void *memory_map;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint64_t memory_descriptor_version;
    uint64_t kernel_base;
    uint64_t kernel_image_size;
    uint64_t kernel_allocation_size;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pixels_per_scanline;
    uint32_t framebuffer_pixel_format;
    uint32_t framebuffer_red_mask;
    uint32_t framebuffer_green_mask;
    uint32_t framebuffer_blue_mask;
    uint32_t framebuffer_reserved_mask;
    uint64_t device_tree_base;
    uint64_t device_tree_size;
} AstraBootInfo;

/* Called by the AArch64 entry assembly after the stack is installed. */
void astra_kernel_main(const AstraBootInfo *boot_info);

#endif
