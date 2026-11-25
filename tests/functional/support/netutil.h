#pragma once
/* Free TCP port allocation for functional tests, so each test can run its
 * own microdlnad instance without colliding with another one. */

/* Binds a TCP socket to 127.0.0.1:0, reads back the port the kernel
 * assigned, and closes it. There is an inherent TOCTOU race (nothing stops
 * another process from grabbing the same port before microdlnad binds it),
 * accepted here since the functional suite runs its binaries sequentially
 * on a single machine. Aborts on an unexpected socket error. */
int ftest_alloc_port(void);
