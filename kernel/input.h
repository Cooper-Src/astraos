#ifndef ASTRAOS_INPUT_H
#define ASTRAOS_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_info.h"

typedef enum AstraInputEventType {
    ASTRA_INPUT_EVENT_KEY = 1,
    ASTRA_INPUT_EVENT_POINTER = 2
} AstraInputEventType;

typedef struct AstraInputEvent {
    AstraInputEventType type;
    uint8_t key;
    int32_t movement_x;
    int32_t movement_y;
    int32_t movement_z;
    uint32_t absolute_x;
    uint32_t absolute_y;
    bool absolute_position;
    bool left_button;
    bool right_button;
} AstraInputEvent;

/* Selects firmware keyboard input when requested, otherwise a detected UART. */
bool astra_input_initialize(const AstraBootInfo *boot_info, bool serial_ready);

/* Blocks until a key or pointer event is available. */
bool astra_input_read_event(AstraInputEvent *event_out);

/* Key-only wrapper used by the text shell; pointer events are ignored. */
bool astra_input_read_byte(uint8_t *byte_out);

bool astra_input_pointer_available(void);

/* Converts a UEFI Unicode key to the shell's ASCII input stream. */
bool astra_input_translate_key(uint16_t unicode_char, uint8_t *byte_out);

#endif
