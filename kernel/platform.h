#ifndef ASTRAOS_PLATFORM_H
#define ASTRAOS_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#include "boot_info.h"

/* Resolve the FDT stdout path (including aliases) to a valid arm,pl011 register
 * range. This parses metadata only and never accesses device registers.
 * base_out is unchanged when discovery fails. */
bool astra_platform_find_pl011_uart(const AstraBootInfo *boot_info, uint64_t *base_out);

#endif
