#pragma once
/* Minimal GENA callback server for functional tests: plays the control
 * point's side of UPnP eventing, i.e. receives the NOTIFY requests that
 * microdlnad sends to a subscriber's Callback URL. */

#include "httpclient.h"

/* Listens on 127.0.0.1:0; returns the listening fd and stores the port in
 * *port. Aborts on error. */
int eventsink_open(int *port);

/* Waits up to timeout_ms for one connection on the sink, reads the full
 * request (NOTIFY line, headers, chunked body) into *msg and, if `reply`
 * is set, answers "HTTP/1.1 200 OK" before closing. Returns 0 on success,
 * -1 on timeout or read error. */
int eventsink_accept(int listen_fd, int timeout_ms, int reply, struct http_response *msg);
