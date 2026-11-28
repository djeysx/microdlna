/* Configuration: the options file (-f, readoptionsfile() in microdlna.c)
 * and the CLI options whose effect is visible to a client (-F, -U), plus
 * the invalid values that must make the server exit at startup. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "../support/test.h"
#include "support/fixtures.h"
#include "support/httpclient.h"
#include "support/netutil.h"
#include "support/process.h"
#include "support/ssdpclient.h"

static const char *write_config(const char *content)
{
    static char path[4096];
    snprintf(path, sizeof(path), "%s/microdlna.conf", ftest_mkdir());
    FILE *f = fopen(path, "w");
    if (!f)
        abort();
    fputs(content, f);
    fclose(f);
    return path;
}

/* Returns the rootDesc.xml body (caller frees), or NULL. */
static char *get_rootdesc(int port)
{
    char req[256];
    snprintf(req, sizeof(req),
             "GET /rootDesc.xml HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",
             port);
    struct http_response resp;
    if (http_do_request(port, req, strlen(req), &resp) != 0)
        return NULL;
    char *body = resp.body;
    resp.body = NULL;
    http_response_free(&resp);
    return body;
}

static void expect_exit_1(const char *const *args)
{
    int status = mdlna_expect_fast_exit(ftest_mkdir(), ftest_alloc_port(), args);
    ASSERT_TRUE(WIFEXITED(status));
    if (WIFEXITED(status))
        ASSERT_INT_EQ(WEXITSTATUS(status), 1);
}

TEST(options_file_is_applied)
{
    const char *conf = write_config(
        "# a comment line\n"
        "\n"
        "   friendly_name=Salon TV\n"
        "max_connections=3\n"
        "uuid=12345678-abcd-ef01-2345-6789abcdef01#trailing comment\n");

    int port = ftest_alloc_port();
    const char *args[] = { "-f", conf, NULL };
    struct mdlna_proc proc;
    mdlna_start(&proc, ftest_mkdir(), port, args);

    char *desc = get_rootdesc(port);
    ASSERT_STR_CONTAINS(desc, "<friendlyName>Salon TV</friendlyName>");
    ASSERT_STR_CONTAINS(desc, "<UDN>uuid:12345678-abcd-ef01-2345-6789abcdef01</UDN>");
    free(desc);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(options_file_value_keeps_spaces_before_a_trailing_comment)
{
    /* characterisation: the value runs up to '#' or end of line, and is
     * not right-trimmed */
    const char *conf = write_config("friendly_name=Salon TV # comment\n");

    int port = ftest_alloc_port();
    const char *args[] = { "-f", conf, NULL };
    struct mdlna_proc proc;
    mdlna_start(&proc, ftest_mkdir(), port, args);

    char *desc = get_rootdesc(port);
    ASSERT_STR_CONTAINS(desc, "<friendlyName>Salon TV </friendlyName>");
    free(desc);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(last_option_wins_between_command_line_and_file)
{
    const char *conf = write_config("friendly_name=FromFile\n");
    int port = ftest_alloc_port();

    const char *file_then_cli[] = { "-f", conf, "-F", "FromCli", NULL };
    struct mdlna_proc proc;
    mdlna_start(&proc, ftest_mkdir(), port, file_then_cli);
    char *desc = get_rootdesc(port);
    ASSERT_STR_CONTAINS(desc, "<friendlyName>FromCli</friendlyName>");
    free(desc);
    ASSERT_TRUE(mdlna_stop(&proc));

    port = ftest_alloc_port();
    const char *cli_then_file[] = { "-F", "FromCli", "-f", conf, NULL };
    mdlna_start(&proc, ftest_mkdir(), port, cli_then_file);
    desc = get_rootdesc(port);
    ASSERT_STR_CONTAINS(desc, "<friendlyName>FromFile</friendlyName>");
    free(desc);
    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(uuid_option_is_used_in_description_and_ssdp)
{
    int port = ftest_alloc_port();
    const char *args[] = { "-U", "abcdef01-2345-6789-abcd-ef0123456789", "-F", "Custom", NULL };
    struct mdlna_proc proc;
    mdlna_start(&proc, ftest_mkdir(), port, args);

    char *desc = get_rootdesc(port);
    ASSERT_STR_CONTAINS(desc, "<UDN>uuid:abcdef01-2345-6789-abcd-ef0123456789</UDN>");
    ASSERT_STR_CONTAINS(desc, "<friendlyName>Custom</friendlyName>");
    free(desc);

    char reply[1024];
    ASSERT_INT_EQ(ssdp_msearch("upnp:rootdevice", 1000, reply, sizeof(reply)), 1);
    ASSERT_STR_CONTAINS(reply, "USN: uuid:abcdef01-2345-6789-abcd-ef0123456789::upnp:rootdevice");

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(invalid_options_file_content_exits_at_startup)
{
    const char *unknown[] = { "-f", write_config("no_such_option=1\n"), NULL };
    expect_exit_1(unknown);

    /* options without an argument are not allowed in the file */
    const char *flag[] = { "-f", write_config("foreground=1\n"), NULL };
    expect_exit_1(flag);

    /* the documented format is option=value: spaces around '=' make the
     * name "friendly-name " unknown */
    const char *spaced[] = { "-f", write_config("friendly_name = X\n"), NULL };
    expect_exit_1(spaced);

    const char *missing[] = { "-f", "/no/such/microdlna.conf", NULL };
    expect_exit_1(missing);
}

TEST(invalid_option_values_exit_at_startup)
{
    const char *bad_uuid[] = { "-U", "not-a-uuid", NULL };
    expect_exit_1(bad_uuid);

    const char *short_uuid[] = { "-U", "12345678-abcd", NULL };
    expect_exit_1(short_uuid);

    const char *bad_interval[] = { "-t", "0", NULL };
    expect_exit_1(bad_interval);

    const char *bad_connections[] = { "-c", "0", NULL };
    expect_exit_1(bad_connections);

    const char *bad_level[] = { "-l", "verbose", NULL };
    expect_exit_1(bad_level);

    const char *unknown_flag[] = { "--no-such-flag", NULL };
    expect_exit_1(unknown_flag);
}
