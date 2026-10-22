#include "input.h"

#include "uart.h"

#include <stddef.h>
#include <stdint.h>

#define ASTRA_INPUT_EFI_SUCCESS UINT64_C(0)
#define ASTRA_INPUT_EFI_NOT_READY (UINT64_C(0x8000000000000000) | UINT64_C(6))
#define ASTRA_INPUT_EFI_UNSUPPORTED (UINT64_C(0x8000000000000000) | UINT64_C(3))

enum {
    ASTRA_INPUT_ASCII_MAX = 0x7f,
    ASTRA_INPUT_DAIF_IRQ_MASK_BIT = 7u,
    ASTRA_INPUT_VBAR_ALIGNMENT_MASK = 0x7ffu,
    ASTRA_INPUT_EVENT_CAP = 4u
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
static AstraFirmwareStall g_firmware_stall = NULL;
static void *g_firmware_poll_event = NULL;
static uint64_t g_firmware_exception_level = 0u;
static uint64_t g_firmware_exception_vector = 0u;
static uint64_t g_firmware_daif = 0u;

#if defined(__aarch64__)
/*
 * Firmware services depend on their original vector table. Temporarily restore
 * that vector/interrupt state for each call, then reinstate the kernel state.
 */
static bool firmware_context_enter(AstraFirmwareCallState *state) {
    uint64_t kernel_vector = 0u;
    uint64_t kernel_daif = 0u;

    if (state == NULL || g_firmware_exception_vector == 0u) {
        return false;
    }

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

    return false;
}

/* Restores the kernel's masked interrupt state and exception vectors. */
static void firmware_context_leave(const AstraFirmwareCallState *state) {
    if (state == NULL) {
        return;
    }

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
}
#endif

static uint64_t firmware_read_key(AstraFirmwareInputKey *key_out) {
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_firmware_input == NULL || g_firmware_input->read_key_stroke == NULL ||
        key_out == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }

#if defined(__aarch64__)
    {
        AstraFirmwareCallState state = {0};
        if (!firmware_context_enter(&state)) {
            return ASTRA_INPUT_EFI_UNSUPPORTED;
        }
        status = g_firmware_input->read_key_stroke(g_firmware_input, key_out);
        firmware_context_leave(&state);
    }
#else
    (void)key_out;
#endif

    return status;
}

static uint64_t firmware_read_pointer(AstraFirmwarePointerState *state_out) {
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_firmware_pointer == NULL || g_firmware_pointer->get_state == NULL ||
        state_out == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }

#if defined(__aarch64__)
    {
        AstraFirmwareCallState state = {0};
        if (!firmware_context_enter(&state)) {
            return ASTRA_INPUT_EFI_UNSUPPORTED;
        }
        status = g_firmware_pointer->get_state(g_firmware_pointer, state_out);
        firmware_context_leave(&state);
    }
#else
    (void)state_out;
#endif

    return status;
}

static uint64_t firmware_read_absolute_pointer(
    AstraFirmwareAbsolutePointerState *state_out
) {
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_firmware_absolute_pointer == NULL ||
        g_firmware_absolute_pointer->get_state == NULL || state_out == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }

#if defined(__aarch64__)
    {
        AstraFirmwareCallState state = {0};
        if (!firmware_context_enter(&state)) {
            return ASTRA_INPUT_EFI_UNSUPPORTED;
        }
        status = g_firmware_absolute_pointer->get_state(
            g_firmware_absolute_pointer, state_out
        );
        firmware_context_leave(&state);
    }
#else
    (void)state_out;
#endif

    return status;
}

static bool firmware_stall(uint64_t microseconds) {
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_firmware_stall == NULL) {
        return false;
    }

#if defined(__aarch64__)
    {
        AstraFirmwareCallState state = {0};
        if (!firmware_context_enter(&state)) {
            return false;
        }
        status = g_firmware_stall(microseconds);
        firmware_context_leave(&state);
    }
#else
    (void)microseconds;
#endif

    return status == ASTRA_INPUT_EFI_SUCCESS;
}


static uint64_t firmware_wait_for_input(void) {
    AstraFirmwareCallState state = {0};
    void *events[ASTRA_INPUT_EVENT_CAP] = {NULL, NULL, NULL, NULL};
    uint64_t event_index = UINT64_MAX;
    uint64_t event_count = 0u;
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_wait_for_event == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
    if (g_firmware_input != NULL && g_firmware_input->wait_for_key != NULL) {
        events[event_count] = g_firmware_input->wait_for_key;
        event_count++;
    }
    if (g_firmware_pointer != NULL && g_firmware_pointer->wait_for_input != NULL) {
        events[event_count] = g_firmware_pointer->wait_for_input;
        event_count++;
    }
    if (g_firmware_absolute_pointer != NULL &&
        g_firmware_absolute_pointer->wait_for_input != NULL) {
        events[event_count] = g_firmware_absolute_pointer->wait_for_input;
        event_count++;
    }
    if (g_firmware_poll_event != NULL) {
        events[event_count] = g_firmware_poll_event;
        event_count++;
    }
    if (event_count == 0u) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
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

    if (status == ASTRA_INPUT_EFI_SUCCESS && event_index >= event_count) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
    return status;
}

static void clear_event(AstraInputEvent *event_out) {
    event_out->type = ASTRA_INPUT_EVENT_NONE;
    event_out->key = 0u;
    event_out->pointer_dx = 0;
    event_out->pointer_dy = 0;
    event_out->pointer_dz = 0;
    event_out->left_button = false;
    event_out->right_button = false;
    event_out->absolute = false;
    event_out->absolute_x = UINT64_C(0);
    event_out->absolute_y = UINT64_C(0);
    event_out->absolute_min_x = UINT64_C(0);
    event_out->absolute_max_x = UINT64_C(0);
    event_out->absolute_min_y = UINT64_C(0);
    event_out->absolute_max_y = UINT64_C(0);
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
    g_firmware_stall = NULL;
    g_firmware_poll_event = NULL;
    g_firmware_exception_level = 0u;
    g_firmware_exception_vector = 0u;
    g_firmware_daif = 0u;

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
        g_firmware_stall = boot_info->firmware_stall;
        g_firmware_poll_event = boot_info->firmware_poll_event;
        g_firmware_exception_level = boot_info->firmware_exception_level;
        g_firmware_exception_vector = boot_info->firmware_exception_vector;
        g_firmware_daif = boot_info->firmware_daif;
        if ((boot_info->flags & ASTRA_BOOT_FLAG_UEFI_POINTER) != 0u &&
            boot_info->firmware_pointer_protocol != NULL &&
            boot_info->firmware_pointer_protocol->get_state != NULL &&
            boot_info->firmware_pointer_protocol->wait_for_input != NULL) {
            g_firmware_pointer = boot_info->firmware_pointer_protocol;
        }
        if ((boot_info->flags & ASTRA_BOOT_FLAG_UEFI_ABSOLUTE_POINTER) != 0u &&
            boot_info->firmware_absolute_pointer_protocol != NULL &&
            boot_info->firmware_absolute_pointer_protocol->get_state != NULL &&
            boot_info->firmware_absolute_pointer_protocol->wait_for_input != NULL &&
            boot_info->firmware_absolute_pointer_protocol->mode != NULL &&
            boot_info->firmware_absolute_pointer_protocol->mode->absolute_max_x >
                boot_info->firmware_absolute_pointer_protocol->mode->absolute_min_x &&
            boot_info->firmware_absolute_pointer_protocol->mode->absolute_max_y >
                boot_info->firmware_absolute_pointer_protocol->mode->absolute_min_y &&
            boot_info->firmware_absolute_pointer_protocol->mode->absolute_max_x -
                boot_info->firmware_absolute_pointer_protocol->mode->absolute_min_x <=
                UINT32_MAX &&
            boot_info->firmware_absolute_pointer_protocol->mode->absolute_max_y -
                boot_info->firmware_absolute_pointer_protocol->mode->absolute_min_y <=
                UINT32_MAX) {
            g_firmware_absolute_pointer =
                boot_info->firmware_absolute_pointer_protocol;
        }
#if defined(__aarch64__)
        g_firmware_daif &= ~(UINT64_C(1) << ASTRA_INPUT_DAIF_IRQ_MASK_BIT);
        return true;
#else
        g_firmware_input = NULL;
        g_firmware_pointer = NULL;
        g_firmware_absolute_pointer = NULL;
        g_wait_for_event = NULL;
        g_firmware_stall = NULL;
        g_firmware_poll_event = NULL;
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

bool astra_input_read_event(AstraInputEvent *event_out) {
    if (event_out == NULL) {
        return false;
    }
    clear_event(event_out);

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
            if (astra_input_translate_key(key.unicode_char, &event_out->key)) {
                event_out->type = ASTRA_INPUT_EVENT_KEY;
                return true;
            }
        } else if (status != ASTRA_INPUT_EFI_NOT_READY) {
            return false;
        }

        if (g_firmware_absolute_pointer != NULL) {
            AstraFirmwareAbsolutePointerState pointer = {0};
            AstraFirmwareAbsolutePointerMode *mode =
                g_firmware_absolute_pointer->mode;
            status = firmware_read_absolute_pointer(&pointer);
            if (status == ASTRA_INPUT_EFI_SUCCESS && mode != NULL) {
                event_out->type = ASTRA_INPUT_EVENT_POINTER;
                event_out->absolute = true;
                event_out->absolute_x = pointer.current_x;
                event_out->absolute_y = pointer.current_y;
                event_out->absolute_min_x = mode->absolute_min_x;
                event_out->absolute_max_x = mode->absolute_max_x;
                event_out->absolute_min_y = mode->absolute_min_y;
                event_out->absolute_max_y = mode->absolute_max_y;
                event_out->left_button =
                    (pointer.active_buttons &
                        (uint32_t)ASTRA_ABSOLUTE_POINTER_BUTTON_TOUCH_ACTIVE) != 0u;
                event_out->right_button =
                    (pointer.active_buttons &
                        (uint32_t)ASTRA_ABSOLUTE_POINTER_BUTTON_ALT_ACTIVE) != 0u;
                return true;
            }
            if (status != ASTRA_INPUT_EFI_NOT_READY &&
                status != ASTRA_INPUT_EFI_UNSUPPORTED) {
                g_firmware_absolute_pointer = NULL;
            }
        }

        if (g_firmware_pointer != NULL) {
            AstraFirmwarePointerState pointer = {0};
            status = firmware_read_pointer(&pointer);
            if (status == ASTRA_INPUT_EFI_SUCCESS) {
                event_out->type = ASTRA_INPUT_EVENT_POINTER;
                event_out->pointer_dx = pointer.relative_movement_x;
                event_out->pointer_dy = pointer.relative_movement_y;
                event_out->pointer_dz = pointer.relative_movement_z;
                event_out->left_button = pointer.left_button != 0u;
                event_out->right_button = pointer.right_button != 0u;
                return true;
            }
            if (status != ASTRA_INPUT_EFI_NOT_READY &&
                status != ASTRA_INPUT_EFI_UNSUPPORTED) {
                g_firmware_pointer = NULL;
            }
        }

        /*
         * The periodic poll event is part of WaitForEvent when pointer input
         * is available. Returning to firmware at that cadence lets UEFI USB
         * drivers service reports before the next GetState() call.
         */
        status = firmware_wait_for_input();
        if (status != ASTRA_INPUT_EFI_SUCCESS) {
            if (astra_input_pointer_available() && firmware_stall(UINT64_C(1000))) {
                continue;
            }
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
