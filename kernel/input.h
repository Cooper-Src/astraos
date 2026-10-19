#ifndef ASTRAOS_INPUT_H
#define ASTRAOS_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_info.h"

/* Selects firmware keyboard input when requested, otherwise a detected UART. */
bool astra_input_initialize(const AstraBootInfo *boot_info, bool serial_ready);

/* Blocks until one byte is available. False means the active input path failed. */
bool astra_input_read_byte(uint8_t *byte_out);

/* Reserved byte values for UEFI function keys; ASCII input remains unchanged. */
enum {
    ASTRA_INPUT_KEY_F1 = 0xf1u,
    ASTRA_INPUT_KEY_F2 = 0xf2u,
    ASTRA_INPUT_KEY_F3 = 0xf3u
};

/* Converts UEFI Unicode keys and F1-F3 scan codes to input bytes. */
bool astra_input_translate_key(uint16_t unicode_char, uint8_t *byte_out);
bool astra_input_translate_firmware_key(
    const AstraFirmwareInputKey *key,
    uint8_t *byte_out
);

#endif
