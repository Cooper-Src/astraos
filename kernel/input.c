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
    ASTRA_INPUT_VBAR_ALIGNMENT_MASK = 0x7ffu,
    ASTRA_INPUT_EVENT_CAP = 3u
};

typedef struct AstraFirmwareCallState {
    uint64_t kernel_vector;
    uint64_t kernel_daif;
} AstraFirmwareCallState;

static bool g_use_serial_input = false;
static AstraFirmwareTextInputProtocol *g_firmware_input = NULL;
static AstraFirmwareSimplePointerProtocol *g_firmware_pointer = NULL;
static AstraFirmwarePointerEventQueue *g_firmware_pointer_events = NULL;
static void *g_firmware_pointer_poll_event = NULL;
static AstraFirmwareWaitForEvent g_wait_for_event = NULL;
static uint64_t g_firmware_exception_level = 0u;
static uint64_t g_firmware_exception_vector = 0u;
static uint64_t g_firmware_daif = 0u;
static bool g_last_pointer_left = false;
static bool g_last_pointer_right = false;
static uint64_t g_pointer_poll_count = 0u;

#if defined(__aarch64__)
/*
 * Firmware USB drivers depend on the firmware's exception vectors and IRQ
 * state. Borrow them only for a UEFI call, then restore the kernel state.
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

static int32_t clamp_pointer_delta(int64_t delta) {
    if (delta > INT32_MAX) {
        return INT32_MAX;
    }
    if (delta < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)delta;
}

static bool firmware_poll_pointer(AstraInputEvent *event_out, bool *ready_out) {
    AstraFirmwarePointerEventQueue *queue = g_firmware_pointer_events;
    int64_t dx = 0;
    int64_t dy = 0;
    bool left = false;
    bool right = false;
    bool changed = false;

    if (event_out == NULL || ready_out == NULL) {
        return false;
    }
    *ready_out = false;
    if (g_firmware_pointer == NULL || queue == NULL) {
        return true;
    }

    /*
     * The firmware notification is dispatched synchronously by WaitForEvent.
     * Once that call has returned, the kernel can consume and clear the queue
     * without racing the firmware callback.
     */
    dx = queue->pending_delta_x;
    dy = queue->pending_delta_y;
    queue->pending_delta_x = 0;
    queue->pending_delta_y = 0;
    left = queue->left_button != 0u;
    right = queue->right_button != 0u;

    changed = dx != 0 || dy != 0 ||
        left != g_last_pointer_left || right != g_last_pointer_right;
    g_last_pointer_left = left;
    g_last_pointer_right = right;

    if (changed) {
        event_out->type = ASTRA_INPUT_EVENT_POINTER;
        event_out->key = 0u;
        event_out->pointer_dx = clamp_pointer_delta(dx);
        event_out->pointer_dy = clamp_pointer_delta(dy);
        event_out->left_button_down = left;
        event_out->right_button_down = right;
        *ready_out = true;
    }
    return true;
}

/* Sleep on a firmware input event instead of spinning when the desktop is idle. */
static uint64_t firmware_wait_for_input(uint64_t *event_index_out) {
    AstraFirmwareCallState state = {0};
    void *events[ASTRA_INPUT_EVENT_CAP] = {NULL, NULL};
    uint64_t event_count = 0u;
    uint64_t event_index = UINT64_MAX;
    uint64_t status = ASTRA_INPUT_EFI_UNSUPPORTED;

    if (g_firmware_input == NULL || g_firmware_input->wait_for_key == NULL ||
        g_wait_for_event == NULL || event_index_out == NULL) {
        return ASTRA_INPUT_EFI_UNSUPPORTED;
    }

    events[event_count] = g_firmware_input->wait_for_key;
    event_count++;
    if (g_firmware_pointer != NULL && g_firmware_pointer->wait_for_input != NULL) {
        events[event_count] = g_firmware_pointer->wait_for_input;
        event_count++;
    }
    if (g_firmware_pointer_poll_event != NULL &&
        event_count < (uint64_t)ASTRA_INPUT_EVENT_CAP) {
        events[event_count] = g_firmware_pointer_poll_event;
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

    if (status != ASTRA_INPUT_EFI_SUCCESS || event_index >= event_count) {
        return status == ASTRA_INPUT_EFI_SUCCESS ?
            ASTRA_INPUT_EFI_UNSUPPORTED : status;
    }
    *event_index_out = event_index;
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
    g_firmware_pointer_events = NULL;
    g_firmware_pointer_poll_event = NULL;
    g_wait_for_event = NULL;
    g_firmware_exception_level = 0u;
    g_firmware_exception_vector = 0u;
    g_firmware_daif = 0u;
    g_last_pointer_left = false;
    g_last_pointer_right = false;
    g_pointer_poll_count = 0u;

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
        g_firmware_pointer = boot_info->firmware_pointer_protocol;
        g_firmware_pointer_events = boot_info->firmware_pointer_events;
        g_firmware_pointer_poll_event = boot_info->firmware_pointer_poll_event;
        if (g_firmware_pointer != NULL &&
            (g_firmware_pointer->get_state == NULL ||
                g_firmware_pointer->wait_for_input == NULL)) {
            g_firmware_pointer = NULL;
        }
        g_wait_for_event = boot_info->firmware_wait_for_event;
        g_firmware_exception_level = boot_info->firmware_exception_level;
        g_firmware_exception_vector = boot_info->firmware_exception_vector;
        g_firmware_daif = boot_info->firmware_daif;
#if defined(__aarch64__)
        g_firmware_daif &= ~(UINT64_C(1) << ASTRA_INPUT_DAIF_IRQ_MASK_BIT);
        return true;
#else
        g_firmware_input = NULL;
        g_firmware_pointer = NULL;
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

bool astra_input_pointer_available(void) {
    return !g_use_serial_input && g_firmware_pointer != NULL;
}

bool astra_input_read_event(AstraInputEvent *event_out) {
    if (event_out == NULL) {
        return false;
    }

    if (g_use_serial_input) {
        event_out->type = ASTRA_INPUT_EVENT_KEY;
        event_out->key = astra_uart_get_byte();
        event_out->pointer_dx = 0;
        event_out->pointer_dy = 0;
        event_out->left_button_down = false;
        event_out->right_button_down = false;
        return true;
    }

    if (g_firmware_input == NULL || g_wait_for_event == NULL) {
        return false;
    }

    for (;;) {
        AstraFirmwareInputKey key = {0};
        uint8_t translated = 0u;
        uint64_t status = firmware_read_key(&key);
        bool pointer_ready = false;

        if (status == ASTRA_INPUT_EFI_SUCCESS &&
            astra_input_translate_key(key.unicode_char, &translated)) {
            event_out->type = ASTRA_INPUT_EVENT_KEY;
            event_out->key = translated;
            event_out->pointer_dx = 0;
            event_out->pointer_dy = 0;
            event_out->left_button_down = g_last_pointer_left;
            event_out->right_button_down = g_last_pointer_right;
            return true;
        }
        if (status != ASTRA_INPUT_EFI_SUCCESS &&
            status != ASTRA_INPUT_EFI_NOT_READY) {
            return false;
        }

        if (!firmware_poll_pointer(event_out, &pointer_ready)) {
            return false;
        }
        if (pointer_ready) {
            return true;
        }

        {
            uint64_t event_index = UINT64_MAX;
            status = firmware_wait_for_input(&event_index);
            if (status != ASTRA_INPUT_EFI_SUCCESS) {
                return false;
            }
        }
    }
}

bool astra_input_read_byte(uint8_t *byte_out) {
    if (byte_out == NULL) {
        return false;
    }

    for (;;) {
        AstraInputEvent event = {0};
        if (!astra_input_read_event(&event)) {
            return false;
        }
        if (event.type == ASTRA_INPUT_EVENT_KEY) {
            *byte_out = event.key;
            return true;
        }
    }
}
