#ifndef ASTRAOS_FRAMEBUFFER_H
#define ASTRAOS_FRAMEBUFFER_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_info.h"

/* Uses UEFI GOP metadata captured by the loader. It calls no firmware APIs. */
void astra_framebuffer_init(const AstraBootInfo *boot_info);
bool astra_framebuffer_is_active(void);
void astra_framebuffer_clear(void);
void astra_framebuffer_put_byte(uint8_t value);

#endif
