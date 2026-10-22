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
    ASTRA_INPUT_MAX_WAIT_EVENTS = 3u
};

typedef struct AstraFirmwareCallState {
    uint64_t kernel_vector;
    uint64_t kernel_daif;
} AstraFirmwareCallState;

static bool g_use_serial_input = false;
static AstraFirmwareTextInputProtocol *g_firmware_input = NULL;
static AstraFirmwareSimplePointerProtocol *g_simple_pointer = NULL;
static AstraFirmwareAbsolutePointerProtocol *g_absolute_pointer = NULL;
static AstraFirmwareWaitForEvent g_wait_for_event = NULL;
static uint64_t g_firmware_exception_level = 0u;
static uint64_t g_firmware_exception_vector = 0u;
static uint64_t g_firmware_daif = 0u;

#if defined(__aarch64__)
/* Firmware services need their original vector table and an unmasked IRQ path. */
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
        AstraFirmwareCallState state;
        state.kernel_vector = 0u;
        state.kernel_daif = 0u;
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

static uint64_t firmware_wait_for_events(
    void **events,
    uint64_t event_count,
    uint64_t *event_index_out
) {
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_wait_for_event == NULL || events == NULL || event_count == 0u ||
        event_count > (uint64_t)ASTRA_INPUT_MAX_WAIT_EVENTS || event_index_out == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
#if defined(__aarch64__)
    {
        AstraFirmwareCallState state;
        state.kernel_vector = 0u;
        state.kernel_daif = 0u;
        if (!firmware_context_enter(&state)) {
            return ASTRA_INPUT_EFI_UNSUPPORTED;
        }
        status = g_wait_for_event(event_count, events, event_index_out);
        firmware_context_leave(&state);
    }
#else
    (void)events;
    (void)event_count;
    (void)event_index_out;
#endif
    return status;
}

static uint64_t firmware_read_simple_pointer(AstraFirmwareSimplePointerState *state_out) {
    if (g_simple_pointer == NULL || g_simple_pointer->get_state == NULL ||
        state_out == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
#if defined(__aarch64__)
    {
        AstraFirmwareCallState state;
        uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;
        state.kernel_vector = 0u;
        state.kernel_daif = 0u;
        if (!firmware_context_enter(&state)) {
            return ASTRA_INPUT_EFI_UNSUPPORTED;
        }
        status = g_simple_pointer->get_state(g_simple_pointer, state_out);
        firmware_context_leave(&state);
        return status;
    }
#else
    return ASTRA_INPUT_EFI_UNSUPPORTED;
#endif
}

static uint64_t firmware_read_absolute_pointer(AstraFirmwareAbsolutePointerState *state_out) {
    if (g_absolute_pointer == NULL || g_absolute_pointer->get_state == NULL ||
        state_out == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }
#if defined(__aarch64__)
    {
        AstraFirmwareCallState state;
        uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;
        state.kernel_vector = 0u;
        state.kernel_daif = 0u;
        if (!firmware_context_enter(&state)) {
            return ASTRA_INPUT_EFI_UNSUPPORTED;
        }
        status = g_absolute_pointer->get_state(g_absolute_pointer, state_out);
        firmware_context_leave(&state);
        return status;
    }
#else
    return ASTRA_INPUT_EFI_UNSUPPORTED;
#endif
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
    g_simple_pointer = NULL;
    g_absolute_pointer = NULL;
    g_wait_for_event = NULL;
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
#if defined(__aarch64__)
        g_firmware_input = boot_info->firmware_input_protocol;
        g_wait_for_event = boot_info->firmware_wait_for_event;
        g_firmware_exception_level = boot_info->firmware_exception_level;
        g_firmware_exception_vector = boot_info->firmware_exception_vector;
        g_firmware_daif = boot_info->firmware_daif &
            ~(UINT64_C(1) << ASTRA_INPUT_DAIF_IRQ_MASK_BIT);

        if ((boot_info->flags & ASTRA_BOOT_FLAG_UEFI_SIMPLE_POINTER) != 0u &&
            boot_info->firmware_simple_pointer_protocol != NULL &&
            boot_info->firmware_simple_pointer_protocol->get_state != NULL &&
            boot_info->firmware_simple_pointer_protocol->wait_for_input != NULL) {
            g_simple_pointer = boot_info->firmware_simple_pointer_protocol;
        }
        if ((boot_info->flags & ASTRA_BOOT_FLAG_UEFI_ABSOLUTE_POINTER) != 0u &&
            boot_info->firmware_absolute_pointer_protocol != NULL &&
            boot_info->firmware_absolute_pointer_protocol->get_state != NULL &&
            boot_info->firmware_absolute_pointer_protocol->wait_for_input != NULL &&
            boot_info->firmware_absolute_pointer_protocol->mode != NULL) {
            const AstraFirmwareAbsolutePointerMode *mode =
                boot_info->firmware_absolute_pointer_protocol->mode;
            if (mode->max_x > mode->min_x && mode->max_y > mode->min_y &&
                mode->max_x <= UINT64_C(1000000) &&
                mode->max_y <= UINT64_C(1000000)) {
                g_absolute_pointer = boot_info->firmware_absolute_pointer_protocol;
            }
        }
        return true;
#endif
    }
    if (serial_ready) {
        g_use_serial_input = true;
        return true;
    }
    return false;
}

bool astra_input_pointer_available(void) {
    return g_simple_pointer != NULL || g_absolute_pointer != NULL;
}

bool astra_input_read_event(AstraInputEvent *event_out) {
    if (event_out == NULL) {
        return false;
    }
    event_out->kind = ASTRA_INPUT_EVENT_KEY;
    event_out->key = 0u;
    event_out->delta_x = 0;
    event_out->delta_y = 0;
    event_out->absolute_x = 0u;
    event_out->absolute_y = 0u;
    event_out->min_x = 0u;
    event_out->max_x = 0u;
    event_out->min_y = 0u;
    event_out->max_y = 0u;
    event_out->absolute = false;
    event_out->left_button_down = false;
    event_out->right_button_down = false;

    if (g_use_serial_input) {
        uint8_t key = 0u;
        key = astra_uart_get_byte();
        event_out->kind = ASTRA_INPUT_EVENT_KEY;
        event_out->key = key;
        return true;
    }
    if (g_firmware_input == NULL || g_wait_for_event == NULL) {
        return false;
    }

    for (;;) {
        void *events[ASTRA_INPUT_MAX_WAIT_EVENTS];
        uint64_t event_count = 1u;
        uint64_t event_index = UINT64_MAX;
        uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;
        const uint64_t key_index = 0u;
        uint64_t simple_index = UINT64_MAX;
        uint64_t absolute_index = UINT64_MAX;

        events[0] = g_firmware_input->wait_for_key;
        if (g_simple_pointer != NULL) {
            simple_index = event_count;
            events[(size_t)event_count] = g_simple_pointer->wait_for_input;
            event_count++;
        }
        if (g_absolute_pointer != NULL) {
            absolute_index = event_count;
            events[(size_t)event_count] = g_absolute_pointer->wait_for_input;
            event_count++;
        }
        status = firmware_wait_for_events(events, event_count, &event_index);
        if (status != ASTRA_INPUT_EFI_SUCCESS || event_index >= event_count) {
            return false;
        }

        if (event_index == key_index) {
            AstraFirmwareInputKey key;
            uint8_t translated = 0u;
            uint64_t key_status = ASTRA_INPUT_EFI_UNSUPPORTED;
            key.scan_code = 0u;
            key.unicode_char = 0u;
            key_status = firmware_read_key(&key);
            if (key_status == ASTRA_INPUT_EFI_NOT_READY) {
                continue;
            }
            if (key_status != ASTRA_INPUT_EFI_SUCCESS) {
                return false;
            }
            if (!astra_input_translate_key(key.unicode_char, &translated)) {
                continue;
            }
            event_out->kind = ASTRA_INPUT_EVENT_KEY;
            event_out->key = translated;
            return true;
        }

        if (event_index == simple_index && g_simple_pointer != NULL) {
            AstraFirmwareSimplePointerState state;
            state.relative_x = 0;
            state.relative_y = 0;
            state.relative_z = 0;
            state.left_button = 0u;
            state.right_button = 0u;
            status = firmware_read_simple_pointer(&state);
            if (status == ASTRA_INPUT_EFI_NOT_READY) {
                continue;
            }
            if (status != ASTRA_INPUT_EFI_SUCCESS) {
                g_simple_pointer = NULL;
                continue;
            }
            event_out->kind = ASTRA_INPUT_EVENT_POINTER;
            event_out->delta_x = state.relative_x;
            event_out->delta_y = state.relative_y;
            event_out->left_button_down = state.left_button != 0u;
            event_out->right_button_down = state.right_button != 0u;
            return true;
        }

        if (event_index == absolute_index && g_absolute_pointer != NULL) {
            AstraFirmwareAbsolutePointerState state;
            const AstraFirmwareAbsolutePointerMode *mode =
                g_absolute_pointer->mode;
            state.current_x = 0u;
            state.current_y = 0u;
            state.current_z = 0u;
            state.active_buttons = 0u;
            status = firmware_read_absolute_pointer(&state);
            if (status == ASTRA_INPUT_EFI_NOT_READY) {
                continue;
            }
            if (status != ASTRA_INPUT_EFI_SUCCESS || mode == NULL ||
                mode->max_x <= mode->min_x || mode->max_y <= mode->min_y ||
                mode->max_x > UINT64_C(1000000) || mode->max_y > UINT64_C(1000000)) {
                g_absolute_pointer = NULL;
                continue;
            }
            event_out->kind = ASTRA_INPUT_EVENT_POINTER;
            event_out->absolute = true;
            event_out->absolute_x = state.current_x;
            event_out->absolute_y = state.current_y;
            event_out->min_x = mode->min_x;
            event_out->max_x = mode->max_x;
            event_out->min_y = mode->min_y;
            event_out->max_y = mode->max_y;
            event_out->left_button_down = (state.active_buttons & 1u) != 0u;
            event_out->right_button_down = (state.active_buttons & 2u) != 0u;
            return true;
        }
    }
}

bool astra_input_read_byte(uint8_t *byte_out) {
    if (byte_out == NULL) {
        return false;
    }
    for (;;) {
        AstraInputEvent event;
        if (!astra_input_read_event(&event)) {
            return false;
        }
        if (event.kind == ASTRA_INPUT_EVENT_KEY) {
            *byte_out = event.key;
            return true;
        }
    }
}
