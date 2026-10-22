#ifndef ASTRAOS_INPUT_H
#define ASTRAOS_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_info.h"

typedef enum AstraInputEventKind {
    ASTRA_INPUT_EVENT_KEY = 1,
    ASTRA_INPUT_EVENT_POINTER
} AstraInputEventKind;

typedef struct AstraInputEvent {
    AstraInputEventKind kind;
    uint8_t key;
    int32_t delta_x;
    int32_t delta_y;
    uint64_t absolute_x;
    uint64_t absolute_y;
    uint64_t min_x;
    uint64_t max_x;
    uint64_t min_y;
    uint64_t max_y;
    bool absolute;
    bool left_button_down;
    bool right_button_down;
} AstraInputEvent;

/* Selects firmware input when requested, otherwise the detected serial console. */
bool astra_input_initialize(const AstraBootInfo *boot_info, bool serial_ready);

/* Blocks for the next keyboard or pointer event. False means input has failed. */
bool astra_input_read_event(AstraInputEvent *event_out);

/* Keyboard-only compatibility wrapper used by the serial shell. */
bool astra_input_read_byte(uint8_t *byte_out);

/* True when UEFI exposed at least one usable relative or absolute pointer. */
bool astra_input_pointer_available(void);

/* Converts a UEFI Unicode key to the shell's ASCII input stream. */
bool astra_input_translate_key(uint16_t unicode_char, uint8_t *byte_out);

#endif
