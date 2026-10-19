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
static AstraFirmwareWaitForEvent g_wait_for_event = NULL;
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

bool astra_input_translate_key(uint16_t unicode_char, uint8_t *byte_out) {
    if (byte_out == NULL || unicode_char == 0u ||
        unicode_char > (uint16_t)ASTRA_INPUT_ASCII_MAX) {
        return false;
    }

    *byte_out = (uint8_t)unicode_char;
    return true;
}

bool astra_input_translate_firmware_key(
    const AstraFirmwareInputKey *key,
    uint8_t *byte_out
) {
    if (key == NULL || byte_out == NULL) {
        return false;
    }
    if (astra_input_translate_key(key->unicode_char, byte_out)) {
        return true;
    }
    switch (key->scan_code) {
        case UINT16_C(0x000b):
            *byte_out = (uint8_t)ASTRA_INPUT_KEY_F1;
            return true;
        case UINT16_C(0x000c):
            *byte_out = (uint8_t)ASTRA_INPUT_KEY_F2;
            return true;
        case UINT16_C(0x000d):
            *byte_out = (uint8_t)ASTRA_INPUT_KEY_F3;
            return true;
        default:
            return false;
    }
}

bool astra_input_initialize(const AstraBootInfo *boot_info, bool serial_ready) {
    g_use_serial_input = false;
    g_firmware_input = NULL;
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
        g_firmware_input = boot_info->firmware_input_protocol;
        g_wait_for_event = boot_info->firmware_wait_for_event;
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
            if (astra_input_translate_firmware_key(&key, byte_out)) {
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
