/* Tests for upnpdescgen.c (UPnP device/service description XML).
 * upnpdescgen.o is excluded from this binary's link. */

#include "../upnpdescgen.c"

#include <stdlib.h>

#include "test.h"
#include "helpers.h"

/* Checks that every opening tag in a gen_xml()-produced document has a
 * matching closing tag, in order, using a small stack - this generator has
 * no self-closing tags and no '>' inside attribute values, so a tag is
 * simply the text between '<' and the next '>'. */
static void assert_tags_balanced(const char *xml)
{
    char *stack[64];
    int top = -1;
    const char *p = xml;

    while ((p = strchr(p, '<')) != NULL)
    {
        const char *end = strchr(p, '>');
        if (!end)
        {
            tests_fail(__FILE__, __LINE__, "unterminated tag in output");
            return;
        }

        const char *tag = p + 1;
        int len = (int)(end - tag);

        if (*tag == '?')
        {
            /* processing instruction, e.g. <?xml version="1.0"?> - self
             * terminated, not an element */
            p = end + 1;
            continue;
        }

        if (*tag == '/')
        {
            tag++;
            len--;
            if (top < 0)
            {
                tests_fail(__FILE__, __LINE__, "closing tag with empty stack: %.*s",
                           len, tag);
                return;
            }
            if (strlen(stack[top]) != (size_t)len || strncmp(stack[top], tag, len) != 0)
            {
                tests_fail(__FILE__, __LINE__, "expected closing </%s>, got </%.*s>",
                           stack[top], len, tag);
                return;
            }
            free(stack[top]);
            top--;
        }
        else
        {
            int namelen = 0;
            while (namelen < len && tag[namelen] != ' ')
                namelen++;
            top++;
            if (top >= 64)
            {
                tests_fail(__FILE__, __LINE__, "tag nesting too deep");
                return;
            }
            stack[top] = strndup(tag, (size_t)namelen);
        }
        p = end + 1;
    }

    if (top != -1)
        tests_fail(__FILE__, __LINE__, "%d unclosed tag(s), innermost <%s>",
                   top + 1, stack[top]);
    while (top >= 0)
        free(stack[top--]);
}

TEST(gen_root_desc_is_well_formed_and_has_expected_content)
{
    struct capture *c = capture_open();

    gen_root_desc(&c->st);
    chunk_print_end(&c->st);

    char *out = dechunk(capture_text(c));
    assert_tags_balanced(out);

    ASSERT_STR_CONTAINS(out, uuidvalue);
    ASSERT_STR_CONTAINS(out, friendly_name);
    ASSERT_STR_CONTAINS(out, "<serviceList>");
    ASSERT_STR_CONTAINS(out, "ContentDirectory");
    ASSERT_STR_CONTAINS(out, "ConnectionManager");
    ASSERT_STR_CONTAINS(out, "X_MS_MediaReceiverRegistrar");
    ASSERT_STR_CONTAINS(out, "/icons/sm.png");
    ASSERT_STR_CONTAINS(out, "/icons/lrg.jpg");

    char *null_leak = strstr(out, "(null)");
    ASSERT_NULL(null_leak);

    free(out);
    capture_close(c);
}

TEST(send_content_directory_scpd_has_actions)
{
    struct capture *c = capture_open();

    send_content_directory(&c->st);
    chunk_print_end(&c->st);

    char *out = dechunk(capture_text(c));
    assert_tags_balanced(out);
    ASSERT_STR_CONTAINS(out, "<actionList>");
    ASSERT_STR_CONTAINS(out, "<action><name>Browse</name>");

    free(out);
    capture_close(c);
}

TEST(send_connection_manager_scpd_has_actions)
{
    struct capture *c = capture_open();

    send_connection_manager(&c->st);
    chunk_print_end(&c->st);

    char *out = dechunk(capture_text(c));
    assert_tags_balanced(out);
    ASSERT_STR_CONTAINS(out, "<actionList>");
    ASSERT_STR_CONTAINS(out, "GetProtocolInfo");

    free(out);
    capture_close(c);
}

TEST(send_x_ms_media_receiver_registrar_scpd_has_actions)
{
    struct capture *c = capture_open();

    send_x_ms_media_receiver_registrar(&c->st);
    chunk_print_end(&c->st);

    char *out = dechunk(capture_text(c));
    assert_tags_balanced(out);
    ASSERT_STR_CONTAINS(out, "<actionList>");

    free(out);
    capture_close(c);
}

TEST(get_resource_protocol_info_values_lists_mp4)
{
    struct capture *c = capture_open();

    get_resource_protocol_info_values(&c->st);
    chunk_print_end(&c->st);

    ASSERT_STR_CONTAINS(capture_text(c), "http-get:*:video/mp4:*");

    capture_close(c);
}
