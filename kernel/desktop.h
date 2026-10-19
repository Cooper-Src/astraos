#ifndef ASTRAOS_DESKTOP_H
#define ASTRAOS_DESKTOP_H

#include <stdbool.h>

#include "vfs.h"

/* Runs the keyboard-driven graphical desktop until Escape returns to the shell. */
bool astra_desktop_run(AstraVfs *filesystem);

#endif
