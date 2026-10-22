#ifndef ASTRAOS_INPUT_H
#define ASTRAOS_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_info.h"

typedef enum AstraInputEventKind {
    ASTRA_INPUT_EVENT_NONE = 0,
    ASTRA_INPUT_EVENT_KEY,
    ASTRA_INPUT_EVENT_POINTER
} AstraInputEventKind;

typedef struct AstraInputEvent {
    AstraInputEventKind kind;
    uint8_t key;
    int32_t delta_x;
    int32_t delta_y;
    int32_t delta_z;
    bool left_button;
    bool right_button;
} AstraInputEvent;

/* Selects firmware keyboard input when requested, otherwise a detected UART. */
bool astra_input_initialize(const AstraBootInfo *boot_info, bool serial_ready);

/* Blocks until one byte is available. False means the active input path failed. */
bool astra_input_read_byte(uint8_t *byte_out);

/* Waits for keyboard or pointer input, then returns exactly one desktop event. */
bool astra_input_read_event(AstraInputEvent *event_out);

/* True when UEFI supplied a live Simple Pointer Protocol during boot. */
bool astra_input_pointer_available(void);

/* Converts a UEFI Unicode key to the shell's ASCII input stream. */
bool astra_input_translate_key(uint16_t unicode_char, uint8_t *byte_out);

#endif
