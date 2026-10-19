#ifndef ASTRAOS_INPUT_H
#define ASTRAOS_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_info.h"

/* Selects firmware keyboard input when requested, otherwise a detected UART. */
bool astra_input_initialize(const AstraBootInfo *boot_info, bool serial_ready);

/* Blocks until one byte is available. False means the active input path failed. */
bool astra_input_read_byte(uint8_t *byte_out);

/* Converts a UEFI Unicode key to the shell's ASCII input stream. */
bool astra_input_translate_key(uint16_t unicode_char, uint8_t *byte_out);

#endif
