#ifndef ASTRAOS_SYSTEM_H
#define ASTRAOS_SYSTEM_H

#include "vfs.h"

/* Returns a borrowed pointer to the kernel-wide VFS after kernel initialization. */
AstraVfs *astra_system_vfs(void);

#endif
