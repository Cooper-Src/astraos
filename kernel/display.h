#ifndef ASTRAOS_DISPLAY_H
#define ASTRAOS_DISPLAY_H

#include <stdbool.h>

/*
 * Runs AstraOS's in-kernel software display server and desktop compositor.
 * The current implementation owns the GOP framebuffer directly; it does not
 * imply process isolation or a userspace client protocol.
 *
 * Returns true when the user leaves the desktop normally, false if the
 * framebuffer or input path becomes unavailable.
 */
bool astra_display_server_run(void);

#endif
