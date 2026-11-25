/* Stubs for the globals and functions that only microdlna.c/version.c define
 * in the production binary.
 *
 * microdlna.o holds main() plus 7 globals (listening_port, notify_interval,
 * max_connections, mode_systemd, media_dir, friendly_name, uuidvalue) and is
 * never linked into a test binary. version.c's get_microdlna_version()
 * depends on build/version_info.h, generated from git metadata; stubbing it
 * keeps tests independent of git state.
 */

#include <stddef.h>

#include "globalvars.h"

int listening_port = 2800;
int notify_interval = 895;
int max_connections = 10;
int mode_systemd = 0;

char *media_dir = NULL;

char friendly_name[FRIENDLYNAME_MAX_LEN] = "test-host";

char uuidvalue[UUID_LEN] = "uuid:4d696e69-444c-164e-9d41-000000000000";

const char *get_microdlna_version(void)
{
    return "test";
}
