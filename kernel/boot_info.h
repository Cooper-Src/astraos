#ifndef ASTRAOS_BOOT_INFO_H
#define ASTRAOS_BOOT_INFO_H

#include <stdint.h>

#define ASTRA_BOOT_INFO_MAGIC UINT64_C(0x4153545241424f4f)
#define ASTRA_BOOT_INFO_VERSION UINT64_C(6)
#define ASTRA_BOOT_MEMORY_MAP_CAP_BYTES (UINT64_C(16384) * UINT64_C(8))

#define ASTRA_BOOT_FLAG_BOOT_SERVICES_ACTIVE (UINT64_C(1) << 0u)
#define ASTRA_BOOT_FLAG_UEFI_TEXT_INPUT (UINT64_C(1) << 1u)
#define ASTRA_BOOT_FLAG_DESKTOP_MODE (UINT64_C(1) << 2u)
#define ASTRA_BOOT_FLAG_UEFI_POINTER (UINT64_C(1) << 3u)

typedef struct AstraFirmwareInputKey {
    uint16_t scan_code;
    uint16_t unicode_char;
} AstraFirmwareInputKey;

typedef struct AstraFirmwareTextInputProtocol AstraFirmwareTextInputProtocol;

typedef uint64_t (*AstraFirmwareInputReset)(
    AstraFirmwareTextInputProtocol *protocol,
    uint8_t extended_verification
);
typedef uint64_t (*AstraFirmwareReadKeyStroke)(
    AstraFirmwareTextInputProtocol *protocol,
    AstraFirmwareInputKey *key_out
);

struct AstraFirmwareTextInputProtocol {
    AstraFirmwareInputReset reset;
    AstraFirmwareReadKeyStroke read_key_stroke;
    void *wait_for_key;
};

typedef struct AstraFirmwarePointerState {
    int32_t relative_movement_x;
    int32_t relative_movement_y;
    int32_t relative_movement_z;
    uint8_t left_button;
    uint8_t right_button;
} AstraFirmwarePointerState;

typedef struct AstraFirmwarePointerMode {
    uint64_t resolution_x;
    uint64_t resolution_y;
    uint64_t resolution_z;
    uint8_t left_button;
    uint8_t right_button;
} AstraFirmwarePointerMode;

typedef struct AstraFirmwareSimplePointerProtocol AstraFirmwareSimplePointerProtocol;
typedef uint64_t (*AstraFirmwarePointerReset)(
    AstraFirmwareSimplePointerProtocol *protocol,
    uint8_t extended_verification
);
typedef uint64_t (*AstraFirmwareGetPointerState)(
    AstraFirmwareSimplePointerProtocol *protocol,
    AstraFirmwarePointerState *state_out
);

struct AstraFirmwareSimplePointerProtocol {
    AstraFirmwarePointerReset reset;
    AstraFirmwareGetPointerState get_state;
    void *wait_for_input;
    AstraFirmwarePointerMode *mode;
};

/* UEFI WaitForEvent ABI; valid only while the boot services are active. */
typedef uint64_t (*AstraFirmwareWaitForEvent)(
    uint64_t event_count,
    void **events,
    uint64_t *index_out
);

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
    uint64_t acpi_rsdp_base;

    /* Firmware entry points are borrowed, never owned by the kernel. */
    uint64_t flags;
    AstraFirmwareTextInputProtocol *firmware_input_protocol;
    AstraFirmwareSimplePointerProtocol *firmware_pointer_protocol;
    AstraFirmwareWaitForEvent firmware_wait_for_event;
    uint64_t firmware_exception_level;
    uint64_t firmware_exception_vector;
    uint64_t firmware_daif;
} AstraBootInfo;

/* Called by the AArch64 entry assembly after the stack is installed. */
void astra_kernel_main(const AstraBootInfo *boot_info);

#endif
