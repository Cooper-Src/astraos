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
static AstraFirmwareStall g_firmware_stall = NULL;
static bool g_absolute_pointer_not_ready_logged = false;
static bool g_absolute_pointer_state_logged = false;
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

static uint64_t firmware_wait_for_key(void) {
    AstraFirmwareCallState state = {0};
    void *events[1] = {NULL};
    uint64_t event_index = UINT64_MAX;
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_firmware_input == NULL || g_firmware_input->wait_for_key == NULL ||
        g_wait_for_event == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
    events[0] = g_firmware_input->wait_for_key;

#if defined(__aarch64__)
    if (!firmware_context_enter(&state)) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
    status = g_wait_for_event(UINT64_C(1), events, &event_index);
    firmware_context_leave(&state);
#else
    (void)state;
    (void)events;
    (void)event_index;
#endif

    if (status == ASTRA_INPUT_EFI_SUCCESS && event_index != UINT64_C(0)) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
    return status;
}

static uint64_t firmware_read_pointer_state(AstraFirmwarePointerState *state_out) {
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

/* Wait on keyboard and mouse together, rather than sleeping on the keyboard alone. */
static bool firmware_wait_for_activity(void) {
    AstraFirmwareCallState state = {0};
    void *events[3];
    size_t event_count = 0u;
    uint64_t event_index = UINT64_MAX;
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

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
    if (event_count == 0u || g_wait_for_event == NULL) {
        return false;
    }

#if defined(__aarch64__)
    if (!firmware_context_enter(&state)) {
        return false;
    }
    status = g_wait_for_event((uint64_t)event_count, events, &event_index);
    firmware_context_leave(&state);
#else
    (void)state;
    (void)events;
    (void)event_index;
#endif

    return status == ASTRA_INPUT_EFI_SUCCESS && event_index < (uint64_t)event_count;
}


/* Stall for a bounded interval while keeping firmware's exception vectors active. */
static bool firmware_stall(uint64_t microseconds) {
    AstraFirmwareCallState state = {0};
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_firmware_stall == NULL) {
        return false;
    }
#if defined(__aarch64__)
    if (!firmware_context_enter(&state)) {
        return false;
    }
    status = g_firmware_stall(microseconds);
    firmware_context_leave(&state);
#else
    (void)state;
    (void)microseconds;
#endif
    return status == ASTRA_INPUT_EFI_SUCCESS;
}

static uint64_t firmware_read_absolute_pointer_state(
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
    g_absolute_pointer_not_ready_logged = false;
    g_absolute_pointer_state_logged = false;
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
                boot_info->firmware_absolute_pointer_protocol->mode->absolute_min_y &&
            boot_info->firmware_absolute_pointer_protocol->mode->absolute_max_x -
                boot_info->firmware_absolute_pointer_protocol->mode->absolute_min_x <=
                UINT32_MAX &&
            boot_info->firmware_absolute_pointer_protocol->mode->absolute_max_y -
                boot_info->firmware_absolute_pointer_protocol->mode->absolute_min_y <=
                UINT32_MAX) {
            g_firmware_absolute_pointer = boot_info->firmware_absolute_pointer_protocol;
        }
        g_firmware_exception_level = boot_info->firmware_exception_level;
        g_firmware_exception_vector = boot_info->firmware_exception_vector;
        g_firmware_daif = boot_info->firmware_daif;
#if defined(__aarch64__)
        g_firmware_daif &= ~(UINT64_C(1) << ASTRA_INPUT_DAIF_IRQ_MASK_BIT);
        return true;
#else
        g_firmware_input = NULL;
        g_wait_for_event = NULL;
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

bool astra_input_read_byte(uint8_t *byte_out) {
    if (byte_out == NULL) {
        return false;
    }

    if (g_use_serial_input) {
        *byte_out = astra_uart_get_byte();
        return true;
    }

    if (g_firmware_input == NULL || g_wait_for_event == NULL) {
        return false;
    }

    for (;;) {
        AstraFirmwareInputKey key = {0};
        uint64_t status = firmware_read_key(&key);

        if (status == ASTRA_INPUT_EFI_SUCCESS) {
            if (astra_input_translate_key(key.unicode_char, byte_out)) {
                return true;
            }
            continue;
        }
        if (status != ASTRA_INPUT_EFI_NOT_READY) {
            return false;
        }

        status = firmware_wait_for_key();
        if (status != ASTRA_INPUT_EFI_SUCCESS) {
            return false;
        }
    }
}

bool astra_input_pointer_available(void) {
    return g_firmware_pointer != NULL || g_firmware_absolute_pointer != NULL;
}

static void input_event_clear(AstraInputEvent *event_out) {
    event_out->kind = ASTRA_INPUT_EVENT_NONE;
    event_out->key = 0u;
    event_out->delta_x = 0;
    event_out->delta_y = 0;
    event_out->delta_z = 0;
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

bool astra_input_read_event(AstraInputEvent *event_out) {
    if (event_out == NULL) {
        return false;
    }
    input_event_clear(event_out);

    if (g_use_serial_input) {
        event_out->key = astra_uart_get_byte();
        event_out->kind = ASTRA_INPUT_EVENT_KEY;
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
                event_out->kind = ASTRA_INPUT_EVENT_KEY;
                return true;
            }
        } else if (status != ASTRA_INPUT_EFI_NOT_READY) {
            return false;
        }

        /*
         * Prefer absolute coordinates when available; firmware-backed tablets
         * can then position the cursor exactly in graphical frontends such as VNC.
         */
        if (g_firmware_absolute_pointer != NULL) {
            AstraFirmwareAbsolutePointerState pointer_state;
            const AstraFirmwareAbsolutePointerMode *mode =
                g_firmware_absolute_pointer->mode;
            status = firmware_read_absolute_pointer_state(&pointer_state);
            if (status == ASTRA_INPUT_EFI_NOT_READY &&
                !g_absolute_pointer_not_ready_logged) {
                g_absolute_pointer_not_ready_logged = true;
                astra_uart_puts(
                    "desktop: absolute pointer GetState returned EFI_NOT_READY; polling for changes.\n"
                );
            }
            if (status == ASTRA_INPUT_EFI_SUCCESS) {
                if (!g_absolute_pointer_state_logged) {
                    g_absolute_pointer_state_logged = true;
                    astra_uart_puts("desktop: absolute pointer state received x=");
                    astra_uart_put_u32((uint32_t)pointer_state.current_x);
                    astra_uart_puts(" y=");
                    astra_uart_put_u32((uint32_t)pointer_state.current_y);
                    astra_uart_puts(" buttons=");
                    astra_uart_put_u32(pointer_state.active_buttons);
                    astra_uart_puts("\n");
                }
                event_out->kind = ASTRA_INPUT_EVENT_POINTER;
                event_out->absolute = true;
                event_out->absolute_x = pointer_state.current_x;
                event_out->absolute_y = pointer_state.current_y;
                event_out->absolute_min_x = mode->absolute_min_x;
                event_out->absolute_max_x = mode->absolute_max_x;
                event_out->absolute_min_y = mode->absolute_min_y;
                event_out->absolute_max_y = mode->absolute_max_y;
                event_out->left_button =
                    (pointer_state.active_buttons &
                        (uint32_t)ASTRA_ABSOLUTE_POINTER_BUTTON_TOUCH_ACTIVE) != 0u;
                event_out->right_button =
                    (pointer_state.active_buttons &
                        (uint32_t)ASTRA_ABSOLUTE_POINTER_BUTTON_ALT_ACTIVE) != 0u;
                return true;
            }
            if (status != ASTRA_INPUT_EFI_NOT_READY &&
                status != ASTRA_INPUT_EFI_UNSUPPORTED) {
                g_firmware_absolute_pointer = NULL;
            }
        }

        if (g_firmware_pointer != NULL) {
            AstraFirmwarePointerState pointer_state;
            status = firmware_read_pointer_state(&pointer_state);
            if (status == ASTRA_INPUT_EFI_SUCCESS) {
                event_out->kind = ASTRA_INPUT_EVENT_POINTER;
                event_out->delta_x = pointer_state.relative_movement_x;
                event_out->delta_y = pointer_state.relative_movement_y;
                event_out->delta_z = pointer_state.relative_movement_z;
                event_out->left_button = pointer_state.left_button != 0u;
                event_out->right_button = pointer_state.right_button != 0u;
                return true;
            }
            if (status != ASTRA_INPUT_EFI_NOT_READY &&
                status != ASTRA_INPUT_EFI_UNSUPPORTED) {
                g_firmware_pointer = NULL;
            }
        }

        if (astra_input_pointer_available()) {
            if (!firmware_stall(UINT64_C(1000))) {
                return false;
            }
            continue;
        }
        if (!firmware_wait_for_activity()) {
            return false;
        }
    }
}
