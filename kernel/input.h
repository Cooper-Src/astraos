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
    int32_t pointer_dx;
    int32_t pointer_dy;
    bool left_button_down;
    bool right_button_down;
} AstraInputEvent;

/* Selects firmware keyboard input when requested, otherwise a detected UART. */
bool astra_input_initialize(const AstraBootInfo *boot_info, bool serial_ready);

/* Returns a keyboard or mouse event. Firmware mode sleeps on keyboard/mouse events. */
bool astra_input_read_event(AstraInputEvent *event_out);

/* Blocks until a keyboard byte is available; pointer events are ignored. */
bool astra_input_read_byte(uint8_t *byte_out);

/* True when an initialized UEFI relative-pointer device is available. */
bool astra_input_pointer_available(void);

/* Converts a UEFI Unicode key to the shell's ASCII input stream. */
bool astra_input_translate_key(uint16_t unicode_char, uint8_t *byte_out);

#endif
