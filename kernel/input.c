#include "input.h"

#include "uart.h"

#include <stddef.h>
#include <stdint.h>

#define ASTRA_INPUT_EFI_SUCCESS UINT64_C(0)
#define ASTRA_INPUT_EFI_NOT_READY \
    (UINT64_C(0x8000000000000000) | UINT64_C(6))
#define ASTRA_INPUT_EFI_UNSUPPORTED \
    (UINT64_C(0x8000000000000000) | UINT64_C(3))

enum {
    ASTRA_INPUT_ASCII_MAX = 0x7f,
    ASTRA_INPUT_DAIF_IRQ_MASK_BIT = 7u,
    ASTRA_INPUT_VBAR_ALIGNMENT_MASK = 0x7ffu
};

typedef struct AstraFirmwareCallState {
    uint64_t kernel_vector;
    uint64_t kernel_daif;
} AstraFirmwareCallState;

static bool g_use_serial_input = false;
static AstraFirmwareTextInputProtocol *g_firmware_input = NULL;
static AstraFirmwareSimplePointerProtocol *g_firmware_pointer = NULL;
static AstraFirmwareAbsolutePointerProtocol *g_firmware_absolute_pointer = NULL;
static AstraFirmwareWaitForEvent g_wait_for_event = NULL;
static void *g_firmware_poll_timer_event = NULL;
static bool g_pointer_polling_active = false;
static uint32_t g_pointer_screen_width = 1u;
static uint32_t g_pointer_screen_height = 1u;
static uint64_t g_firmware_exception_level = 0u;
static uint64_t g_firmware_exception_vector = 0u;
static uint64_t g_firmware_daif = 0u;

#if defined(__aarch64__)
/*
 * Temporarily restores firmware exception handling while calling UEFI input
 * services. The kernel masks interrupts and installs its own vectors at entry;
 * firmware USB/input event handlers need their original vector table.
 */
static bool firmware_context_enter(AstraFirmwareCallState *state) {
    uint64_t kernel_vector = 0u;
    uint64_t kernel_daif = 0u;

    if (state == NULL || g_firmware_exception_vector == 0u) {
        return false;
    }

#if defined(__aarch64__)
    if (g_firmware_exception_level == UINT64_C(1)) {
        __asm__ volatile(
            "mrs %0, VBAR_EL1\n\t"
            "mrs %1, DAIF"
            : "=r"(kernel_vector), "=r"(kernel_daif)
        );
        state->kernel_vector = kernel_vector;
        state->kernel_daif = kernel_daif;
        __asm__ volatile(
            "msr VBAR_EL1, %0\n\t"
            "isb\n\t"
            "msr DAIF, %1\n\t"
            "isb"
            :
            : "r"(g_firmware_exception_vector), "r"(g_firmware_daif)
            : "memory"
        );
        return true;
    }
    if (g_firmware_exception_level == UINT64_C(2)) {
        __asm__ volatile(
            "mrs %0, VBAR_EL2\n\t"
            "mrs %1, DAIF"
            : "=r"(kernel_vector), "=r"(kernel_daif)
        );
        state->kernel_vector = kernel_vector;
        state->kernel_daif = kernel_daif;
        __asm__ volatile(
            "msr VBAR_EL2, %0\n\t"
            "isb\n\t"
            "msr DAIF, %1\n\t"
            "isb"
            :
            : "r"(g_firmware_exception_vector), "r"(g_firmware_daif)
            : "memory"
        );
        return true;
    }
#else
    (void)kernel_vector;
    (void)kernel_daif;
#endif

    return false;
}

/* Restores the kernel's masked interrupt state and exception vectors. */
static void firmware_context_leave(const AstraFirmwareCallState *state) {
    if (state == NULL) {
        return;
    }

#if defined(__aarch64__)
    if (g_firmware_exception_level == UINT64_C(1)) {
        __asm__ volatile(
            "msr DAIFSet, #0xf\n\t"
            "isb\n\t"
            "msr VBAR_EL1, %0\n\t"
            "isb\n\t"
            "msr DAIF, %1\n\t"
            "isb"
            :
            : "r"(state->kernel_vector), "r"(state->kernel_daif)
            : "memory"
        );
    } else if (g_firmware_exception_level == UINT64_C(2)) {
        __asm__ volatile(
            "msr DAIFSet, #0xf\n\t"
            "isb\n\t"
            "msr VBAR_EL2, %0\n\t"
            "isb\n\t"
            "msr DAIF, %1\n\t"
            "isb"
            :
            : "r"(state->kernel_vector), "r"(state->kernel_daif)
            : "memory"
        );
    }
#else
    (void)state;
#endif
}

#endif

static uint64_t firmware_read_key(AstraFirmwareInputKey *key_out) {
    AstraFirmwareCallState state = {0};
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_firmware_input == NULL || g_firmware_input->read_key_stroke == NULL ||
        key_out == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }

#if defined(__aarch64__)
    if (!firmware_context_enter(&state)) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
    status = g_firmware_input->read_key_stroke(g_firmware_input, key_out);
    firmware_context_leave(&state);
#else
    (void)state;
    (void)key_out;
#endif

    return status;
}

static uint64_t firmware_read_pointer(AstraFirmwarePointerState *state_out) {
    AstraFirmwareCallState state = {0};
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_firmware_pointer == NULL || g_firmware_pointer->get_state == NULL ||
        state_out == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }

#if defined(__aarch64__)
    if (!firmware_context_enter(&state)) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
    status = g_firmware_pointer->get_state(g_firmware_pointer, state_out);
    firmware_context_leave(&state);
#else
    (void)state;
    (void)state_out;
#endif

    return status;
}

/*
 * UEFI pointer motion is expressed in device-specific resolution counts.
 * Convert to a nominal 96-DPI desktop coordinate delta, with a one-pixel
 * minimum so low-resolution/firmware-scaled devices remain responsive.
 */
static int32_t pointer_motion_to_pixels(int32_t movement, uint64_t resolution) {
    int64_t signed_value = (int64_t)movement;
    uint64_t magnitude = (uint64_t)(signed_value < 0 ? -signed_value : signed_value);
    uint64_t pixels = magnitude;

    if (movement == 0) {
        return 0;
    }
    if (resolution != 0u) {
        /* UEFI reports counts/mm; four pixels/mm approximates a 96-DPI panel. */
        pixels = (magnitude * UINT64_C(4) + resolution / UINT64_C(2)) / resolution;
        if (pixels == 0u) {
            pixels = 1u;
        }
    }
    if (pixels > UINT64_C(256)) {
        pixels = UINT64_C(256);
    }
    return signed_value < 0 ? -(int32_t)pixels : (int32_t)pixels;
}

static uint64_t firmware_read_absolute_pointer(
    AstraFirmwareAbsolutePointerState *state_out
) {
    AstraFirmwareCallState state = {0};
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_firmware_absolute_pointer == NULL ||
        g_firmware_absolute_pointer->get_state == NULL || state_out == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }

#if defined(__aarch64__)
    if (!firmware_context_enter(&state)) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
    status = g_firmware_absolute_pointer->get_state(
        g_firmware_absolute_pointer, state_out
    );
    firmware_context_leave(&state);
#else
    (void)state;
    (void)state_out;
#endif

    return status;
}

static uint32_t absolute_axis_to_pixel(
    uint64_t current,
    uint64_t minimum,
    uint64_t maximum,
    uint32_t dimension
) {
    uint64_t range = 0u;
    uint64_t offset = 0u;
    uint64_t pixel_range = 0u;
    uint64_t pixel = 0u;

    if (dimension <= 1u || maximum <= minimum) {
        return 0u;
    }
    pixel_range = (uint64_t)dimension - UINT64_C(1);
    if (current <= minimum) {
        return 0u;
    }
    if (current >= maximum) {
        return dimension - 1u;
    }
    range = maximum - minimum;
    offset = current - minimum;
    if (offset <= UINT64_MAX / pixel_range) {
        pixel = (offset * pixel_range) / range;
    } else {
        const uint64_t divisor = range / pixel_range;
        pixel = divisor == 0u ? pixel_range : offset / divisor;
    }
    if (pixel > pixel_range) {
        pixel = pixel_range;
    }
    return (uint32_t)pixel;
}

static uint64_t firmware_wait_for_input(void) {
    AstraFirmwareCallState state = {0};
    void *events[4] = {NULL, NULL, NULL, NULL};
    uint64_t event_index = UINT64_MAX;
    uint64_t event_count = UINT64_C(1);
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_firmware_input == NULL || g_firmware_input->wait_for_key == NULL ||
        g_wait_for_event == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
    events[0] = g_firmware_input->wait_for_key;
    if (g_firmware_absolute_pointer != NULL &&
        g_firmware_absolute_pointer->wait_for_input != NULL) {
        events[event_count] = g_firmware_absolute_pointer->wait_for_input;
        event_count++;
    }
    if (g_firmware_pointer != NULL && g_firmware_pointer->wait_for_input != NULL) {
        events[event_count] = g_firmware_pointer->wait_for_input;
        event_count++;
    }
    if (g_pointer_polling_active && g_firmware_poll_timer_event != NULL) {
        events[event_count] = g_firmware_poll_timer_event;
        event_count++;
    }

#if defined(__aarch64__)
    if (!firmware_context_enter(&state)) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
    status = g_wait_for_event(event_count, events, &event_index);
    firmware_context_leave(&state);
#else
    (void)state;
    (void)events;
    (void)event_index;
#endif

    if (status == ASTRA_INPUT_EFI_SUCCESS && event_index < event_count) {
        return ASTRA_INPUT_EFI_SUCCESS;
    }
    return status == ASTRA_INPUT_EFI_SUCCESS ?
        ASTRA_INPUT_EFI_UNSUPPORTED : status;
}

bool astra_input_translate_key(uint16_t unicode_char, uint8_t *byte_out) {
    if (byte_out == NULL || unicode_char == 0u ||
        unicode_char > (uint16_t)ASTRA_INPUT_ASCII_MAX) {
        return false;
    }

    *byte_out = (uint8_t)unicode_char;
    return true;
}

bool astra_input_initialize(const AstraBootInfo *boot_info, bool serial_ready) {
    g_use_serial_input = false;
    g_firmware_input = NULL;
    g_firmware_pointer = NULL;
    g_firmware_absolute_pointer = NULL;
    g_wait_for_event = NULL;
    g_firmware_poll_timer_event = NULL;
    g_pointer_polling_active = false;
    g_firmware_exception_level = 0u;
    g_firmware_exception_vector = 0u;
    g_firmware_daif = 0u;
    g_pointer_screen_width = boot_info != NULL && boot_info->framebuffer_width > 1u ?
        boot_info->framebuffer_width : 1u;
    g_pointer_screen_height = boot_info != NULL && boot_info->framebuffer_height > 1u ?
        boot_info->framebuffer_height : 1u;

    if (boot_info != NULL &&
        boot_info->magic == ASTRA_BOOT_INFO_MAGIC &&
        boot_info->version == ASTRA_BOOT_INFO_VERSION &&
        (boot_info->flags & ASTRA_BOOT_FLAG_BOOT_SERVICES_ACTIVE) != 0u &&
        (boot_info->flags & ASTRA_BOOT_FLAG_UEFI_TEXT_INPUT) != 0u &&
        boot_info->firmware_input_protocol != NULL &&
        boot_info->firmware_input_protocol->read_key_stroke != NULL &&
        boot_info->firmware_input_protocol->wait_for_key != NULL &&
        boot_info->firmware_wait_for_event != NULL &&
        (boot_info->firmware_exception_level == UINT64_C(1) ||
            boot_info->firmware_exception_level == UINT64_C(2)) &&
        boot_info->firmware_exception_vector != 0u &&
        (boot_info->firmware_exception_vector &
            (uint64_t)ASTRA_INPUT_VBAR_ALIGNMENT_MASK) == 0u) {
        g_firmware_input = boot_info->firmware_input_protocol;
        g_wait_for_event = boot_info->firmware_wait_for_event;
        g_firmware_poll_timer_event = boot_info->firmware_poll_timer_event;
        g_firmware_exception_level = boot_info->firmware_exception_level;
        g_firmware_exception_vector = boot_info->firmware_exception_vector;
        g_firmware_daif = boot_info->firmware_daif;
#if defined(__aarch64__)
        g_firmware_daif &= ~(UINT64_C(1) << ASTRA_INPUT_DAIF_IRQ_MASK_BIT);
        if ((boot_info->flags & ASTRA_BOOT_FLAG_UEFI_POINTER_INPUT) != 0u &&
            boot_info->firmware_pointer_protocol != NULL &&
            boot_info->firmware_pointer_protocol->get_state != NULL &&
            boot_info->firmware_pointer_protocol->wait_for_input != NULL) {
            g_firmware_pointer = boot_info->firmware_pointer_protocol;
        }
        if ((boot_info->flags & ASTRA_BOOT_FLAG_UEFI_ABSOLUTE_POINTER_INPUT) != 0u &&
            boot_info->firmware_absolute_pointer_protocol != NULL &&
            boot_info->firmware_absolute_pointer_protocol->get_state != NULL &&
            boot_info->firmware_absolute_pointer_protocol->wait_for_input != NULL &&
            boot_info->firmware_absolute_pointer_protocol->mode != NULL &&
            boot_info->firmware_absolute_pointer_protocol->mode->absolute_max_x >
                boot_info->firmware_absolute_pointer_protocol->mode->absolute_min_x &&
            boot_info->firmware_absolute_pointer_protocol->mode->absolute_max_y >
                boot_info->firmware_absolute_pointer_protocol->mode->absolute_min_y) {
            g_firmware_absolute_pointer =
                boot_info->firmware_absolute_pointer_protocol;
        }
        return true;
#else
        g_firmware_input = NULL;
        g_firmware_pointer = NULL;
        g_firmware_absolute_pointer = NULL;
        g_wait_for_event = NULL;
        g_firmware_poll_timer_event = NULL;
        g_firmware_exception_level = 0u;
        g_firmware_exception_vector = 0u;
        g_firmware_daif = 0u;
#endif
    }

    if (serial_ready) {
        g_use_serial_input = true;
        return true;
    }
    return false;
}

bool astra_input_pointer_available(void) {
    return g_firmware_pointer != NULL || g_firmware_absolute_pointer != NULL;
}

void astra_input_set_pointer_polling(bool enabled) {
    g_pointer_polling_active = enabled;
}

bool astra_input_read_event(AstraInputEvent *event_out) {
    if (event_out == NULL) {
        return false;
    }
    *event_out = (AstraInputEvent){0};

    if (g_use_serial_input) {
        event_out->type = ASTRA_INPUT_EVENT_KEY;
        event_out->key = astra_uart_get_byte();
        return true;
    }

    if (g_firmware_input == NULL || g_wait_for_event == NULL) {
        return false;
    }

    for (;;) {
        AstraFirmwareInputKey key = {0};
        uint64_t status = firmware_read_key(&key);

        if (status == ASTRA_INPUT_EFI_SUCCESS) {
            uint8_t translated = 0u;
            if (astra_input_translate_key(key.unicode_char, &translated)) {
                event_out->type = ASTRA_INPUT_EVENT_KEY;
                event_out->key = translated;
                return true;
            }
        } else if (status != ASTRA_INPUT_EFI_NOT_READY) {
            return false;
        }

        if (g_firmware_absolute_pointer != NULL) {
            AstraFirmwareAbsolutePointerState pointer = {0};
            const uint64_t pointer_status =
                firmware_read_absolute_pointer(&pointer);
            if (pointer_status == ASTRA_INPUT_EFI_SUCCESS) {
                const AstraFirmwareAbsolutePointerMode *mode =
                    g_firmware_absolute_pointer->mode;
                event_out->type = ASTRA_INPUT_EVENT_POINTER;
                event_out->absolute_position = true;
                if (mode != NULL) {
                    event_out->absolute_x = absolute_axis_to_pixel(
                        pointer.current_x, mode->absolute_min_x,
                        mode->absolute_max_x, g_pointer_screen_width
                    );
                    event_out->absolute_y = absolute_axis_to_pixel(
                        pointer.current_y, mode->absolute_min_y,
                        mode->absolute_max_y, g_pointer_screen_height
                    );
                }
                event_out->left_button = (pointer.active_buttons & UINT32_C(1)) != 0u;
                event_out->right_button = (pointer.active_buttons & UINT32_C(2)) != 0u;
                return true;
            }
            if (pointer_status != ASTRA_INPUT_EFI_NOT_READY) {
                g_firmware_absolute_pointer = NULL;
            }
        }

        if (g_firmware_pointer != NULL) {
            AstraFirmwarePointerState pointer = {0};
            const uint64_t pointer_status = firmware_read_pointer(&pointer);
            if (pointer_status == ASTRA_INPUT_EFI_SUCCESS) {
                event_out->type = ASTRA_INPUT_EVENT_POINTER;
                event_out->movement_x = pointer_motion_to_pixels(
                    pointer.relative_movement_x,
                    g_firmware_pointer->mode == NULL ? 0u :
                        g_firmware_pointer->mode->resolution_x
                );
                event_out->movement_y = pointer_motion_to_pixels(
                    pointer.relative_movement_y,
                    g_firmware_pointer->mode == NULL ? 0u :
                        g_firmware_pointer->mode->resolution_y
                );
                event_out->movement_z = pointer.relative_movement_z;
                event_out->left_button = pointer.left_button != 0u;
                event_out->right_button = pointer.right_button != 0u;
                return true;
            }
            if (pointer_status != ASTRA_INPUT_EFI_NOT_READY) {
                g_firmware_pointer = NULL;
            }
        }

        status = firmware_wait_for_input();
        if (status != ASTRA_INPUT_EFI_SUCCESS) {
            return false;
        }
    }
}

bool astra_input_read_byte(uint8_t *byte_out) {
    AstraInputEvent event = {0};

    if (byte_out == NULL) {
        return false;
    }
    for (;;) {
        if (!astra_input_read_event(&event)) {
            return false;
        }
        if (event.type == ASTRA_INPUT_EVENT_KEY) {
            *byte_out = event.key;
            return true;
        }
    }
}
