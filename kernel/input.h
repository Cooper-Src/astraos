#ifndef ASTRAOS_INPUT_H
#define ASTRAOS_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_info.h"

typedef enum AstraInputEventType {
    ASTRA_INPUT_EVENT_NONE = 0,
    ASTRA_INPUT_EVENT_KEY,
    ASTRA_INPUT_EVENT_POINTER
} AstraInputEventType;

typedef struct AstraInputEvent {
    AstraInputEventType type;
    uint8_t key;
    int32_t pointer_dx;
    int32_t pointer_dy;
    int32_t pointer_dz;
    bool left_button;
    bool right_button;
    bool absolute;
    uint64_t absolute_x;
    uint64_t absolute_y;
    uint64_t absolute_min_x;
    uint64_t absolute_max_x;
    uint64_t absolute_min_y;
    uint64_t absolute_max_y;
} AstraInputEvent;

/* Selects firmware input when requested, otherwise the detected serial console. */
bool astra_input_initialize(const AstraBootInfo *boot_info, bool serial_ready);

/* Blocks until keyboard or pointer input arrives. Output is caller-owned. */
bool astra_input_read_event(AstraInputEvent *event_out);

/* Blocks until a key arrives; pointer events are ignored for shell compatibility. */
bool astra_input_read_byte(uint8_t *byte_out);

/* Reports whether a usable firmware pointer protocol was handed off by UEFI. */
bool astra_input_pointer_available(void);

/* Converts a UEFI Unicode key to the shell's ASCII input stream. */
bool astra_input_translate_key(uint16_t unicode_char, uint8_t *byte_out);

#endif
