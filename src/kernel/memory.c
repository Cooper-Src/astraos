#include "astra/memory.h"

/* Volatile access prevents the compiler from lowering this implementation back to memset. */
void *memset(void *destination, int value, size_t count) {
    volatile unsigned char *bytes = destination;
    unsigned char byte_value = (unsigned char)value;

    for (size_t i = 0; i < count; i++) {
        bytes[i] = byte_value;
    }
    return destination;
}
