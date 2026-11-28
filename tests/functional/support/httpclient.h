#pragma once
/* Minimal HTTP/1.1 client over a raw socket for black-box functional
 * tests. No keep-alive, no gzip -- just enough to drive the ~10 fixed
 * scenarios in tests/functional. microdlnad itself never keeps a
 * connection open past one response (it always sends "Connection: close"
 * and closes the fd, see upnphttp.c), so one request per connection is all
 * that's ever needed here. */

#include <stddef.h>

struct http_response
{
    int status;
    char *headers;   /* raw header block up to (not including) the final
                       * blank line, NUL-terminated */
    char *body;      /* reassembled body: Content-Length-bounded,
                       * de-chunked, or read-until-EOF, NUL-terminated */
    size_t body_len; /* body length in bytes (the file-serving endpoints
                       * can legitimately contain embedded NULs) */
};

/* Opens a TCP connection to 127.0.0.1:port. Returns the fd (caller owns it
 * and must close() it), or -1 on error. Exposed directly (rather than only
 * through http_do_request()) for scenarios that need to hold a connection
 * open deliberately -- e.g. saturating max_connections by not finishing a
 * read. */
int http_connect(int port);

/* Sends a complete, caller-built request (request line + headers +
 * "\r\n\r\n" + optional body) on `port`, reads the full response, and
 * fills *out. Returns 0 on success, -1 on a connection or framing error
 * (out is zeroed in that case). Always closes its own connection. */
int http_do_request(int port, const char *request, size_t request_len,
                     struct http_response *out);

void http_response_free(struct http_response *r);

/* Reads one complete HTTP message (start line + headers + body) from an
 * open fd into *out: the body is bounded by Content-Length, de-chunked, or
 * -- for a response only -- read until EOF. With is_request set, a message
 * without framing has an empty body (the peer is waiting for our reply,
 * not closing). For a request, out->status is 0 and out->headers starts
 * with the request line. Returns 0, or -1 on error (out zeroed). */
int http_read_message(int fd, int is_request, struct http_response *out);

/* Reads just the status line + header block from an already-open
 * connection on which a request was already sent, deliberately leaving
 * the body (if any) undrained in the socket. Used by tests that need to
 * confirm "the server has started responding" without reading a
 * (possibly huge, or deliberately stalled-on) body -- e.g. to pin a
 * serve_file thread in place while it blocks trying to write more of a
 * file past what fits in the kernel socket buffers. Returns the HTTP
 * status code, or -1 on error. */
int http_read_status_only(int fd);
