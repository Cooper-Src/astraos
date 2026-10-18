#ifndef ASTRA_MEMORY_H
#define ASTRA_MEMORY_H

#include <stddef.h>

/* Fills exactly count bytes; destination must address a writable range of that size. */
void *memset(void *destination, int value, size_t count);

#endif
