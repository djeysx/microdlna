#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "httpclient.h"

#define READ_CHUNK 4096

int http_connect(int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);

    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        close(s);
        return -1;
    }

    return s;
}

/* Reads at least one more chunk of data into *buf, growing it as needed.
 * Returns the number of bytes read (> 0), 0 on EOF, or -1 on error. */
static int recv_more(int fd, unsigned char **buf, size_t *len, size_t *cap)
{
    if (*len + READ_CHUNK > *cap)
    {
        *cap = *len + READ_CHUNK;
        unsigned char *grown = realloc(*buf, *cap);
        if (!grown)
            abort();
        *buf = grown;
    }

    ssize_t n = read(fd, *buf + *len, READ_CHUNK);
    if (n <= 0)
        return (int)n;

    *len += (size_t)n;
    return (int)n;
}

static ssize_t find_crlfcrlf(const unsigned char *buf, size_t len)
{
    for (size_t i = 0; i + 4 <= len; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
            return (ssize_t)i;
    return -1;
}

/* Finds a header line in a NUL-terminated raw header block (case
 * insensitive name match) and returns a pointer to the start of its
 * (whitespace-trimmed) value, or NULL. The returned pointer is only valid
 * for reading up to the next '\r'/'\n'/'\0'. */
static const char *find_header_ci(const char *headers, const char *name)
{
    size_t name_len = strlen(name);
    const char *p = headers;

    while (*p)
    {
        const char *line_end = strstr(p, "\r\n");
        if (!line_end)
            line_end = p + strlen(p);

        if ((size_t)(line_end - p) > name_len
            && strncasecmp(p, name, name_len) == 0 && p[name_len] == ':')
        {
            const char *v = p + name_len + 1;
            while (*v == ' ')
                v++;
            return v;
        }

        if (*line_end == '\0')
            break;
        p = line_end + 2;
    }

    return NULL;
}

static int decode_chunked_body(int fd, unsigned char **buf, size_t *len, size_t *cap,
                               size_t pos, unsigned char **out_body, size_t *out_len)
{
    unsigned char *body = NULL;
    size_t body_len = 0, body_cap = 0;

    for (;;)
    {
        ssize_t line_end;
        for (;;)
        {
            line_end = -1;
            for (size_t i = pos; i + 2 <= *len; i++)
            {
                if ((*buf)[i] == '\r' && (*buf)[i + 1] == '\n')
                {
                    line_end = (ssize_t)i;
                    break;
                }
            }
            if (line_end >= 0)
                break;
            if (recv_more(fd, buf, len, cap) <= 0)
            {
                free(body);
                return -1;
            }
        }

        long chunk_len = strtol((char *)*buf + pos, NULL, 16);
        if (chunk_len < 0)
        {
            free(body);
            return -1;
        }
        pos = (size_t)line_end + 2;

        if (chunk_len == 0)
        {
            while (pos + 2 > *len)
                if (recv_more(fd, buf, len, cap) <= 0)
                {
                    free(body);
                    return -1;
                }
            break;
        }

        while (pos + (size_t)chunk_len + 2 > *len)
            if (recv_more(fd, buf, len, cap) <= 0)
            {
                free(body);
                return -1;
            }

        if (body_len + (size_t)chunk_len + 1 > body_cap)
        {
            body_cap = body_len + (size_t)chunk_len + 1;
            unsigned char *grown = realloc(body, body_cap);
            if (!grown)
                abort();
            body = grown;
        }
        memcpy(body + body_len, *buf + pos, (size_t)chunk_len);
        body_len += (size_t)chunk_len;
        pos += (size_t)chunk_len + 2; /* chunk data + trailing CRLF */
    }

    if (!body)
    {
        body = malloc(1);
        if (!body)
            abort();
    }
    body[body_len] = '\0';
    *out_body = body;
    *out_len = body_len;
    return 0;
}

int http_do_request(int port, const char *request, size_t request_len,
                     struct http_response *out)
{
    memset(out, 0, sizeof(*out));

    int fd = http_connect(port);
    if (fd < 0)
        return -1;

    size_t sent = 0;
    while (sent < request_len)
    {
        ssize_t n = write(fd, request + sent, request_len - sent);
        if (n <= 0)
        {
            close(fd);
            return -1;
        }
        sent += (size_t)n;
    }

    int rc = http_read_message(fd, 0, out);
    close(fd);
    return rc;
}

int http_read_message(int fd, int is_request, struct http_response *out)
{
    memset(out, 0, sizeof(*out));

    unsigned char *buf = NULL;
    size_t len = 0, cap = 0;
    ssize_t header_end;

    for (;;)
    {
        header_end = find_crlfcrlf(buf, len);
        if (header_end >= 0)
            break;
        if (recv_more(fd, &buf, &len, &cap) <= 0)
        {
            free(buf);
            return -1;
        }
    }
    header_end += 4;

    out->headers = malloc((size_t)header_end + 1);
    if (!out->headers)
        abort();
    memcpy(out->headers, buf, (size_t)header_end);
    out->headers[header_end] = '\0';

    const char *status_space = strchr(out->headers, ' ');
    out->status = status_space ? atoi(status_space + 1) : 0;

    int chunked = 0;
    const char *te = find_header_ci(out->headers, "Transfer-Encoding");
    if (te && strncasecmp(te, "chunked", 7) == 0)
        chunked = 1;

    long content_length = -1;
    const char *cl = find_header_ci(out->headers, "Content-Length");
    if (cl)
        content_length = strtol(cl, NULL, 10);

    size_t body_start = (size_t)header_end;

    if (chunked)
    {
        unsigned char *body;
        size_t body_len;
        if (decode_chunked_body(fd, &buf, &len, &cap, body_start, &body, &body_len) != 0)
        {
            free(buf);
            http_response_free(out);
            return -1;
        }
        out->body = (char *)body;
        out->body_len = body_len;
    }
    else if (content_length >= 0)
    {
        while (len < body_start + (size_t)content_length)
            if (recv_more(fd, &buf, &len, &cap) <= 0)
                break; /* short read: use whatever arrived as the body */

        size_t have = len - body_start;
        size_t want = (size_t)content_length;
        size_t take = have < want ? have : want;

        out->body = malloc(take + 1);
        if (!out->body)
            abort();
        memcpy(out->body, buf + body_start, take);
        out->body[take] = '\0';
        out->body_len = take;
    }
    else
    {
        /* a request without framing has no body; a response without
         * framing runs until the server closes the connection */
        if (!is_request)
            for (;;)
                if (recv_more(fd, &buf, &len, &cap) <= 0)
                    break;

        size_t take = len - body_start;
        out->body = malloc(take + 1);
        if (!out->body)
            abort();
        memcpy(out->body, buf + body_start, take);
        out->body[take] = '\0';
        out->body_len = take;
    }

    free(buf);
    return 0;
}

void http_response_free(struct http_response *r)
{
    free(r->headers);
    free(r->body);
    r->headers = NULL;
    r->body = NULL;
}

int http_read_status_only(int fd)
{
    unsigned char *buf = NULL;
    size_t len = 0, cap = 0;
    ssize_t header_end;

    for (;;)
    {
        header_end = find_crlfcrlf(buf, len);
        if (header_end >= 0)
            break;
        if (recv_more(fd, &buf, &len, &cap) <= 0)
        {
            free(buf);
            return -1;
        }
    }

    const unsigned char *space = memchr(buf, ' ', (size_t)header_end);
    int status = space ? atoi((const char *)space + 1) : -1;

    free(buf);
    return status;
}
