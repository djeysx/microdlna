/* SSDP M-SEARCH over multicast, routed through the loopback interface --
 * see tests/functional/support/ssdpclient.h for why "-i lo" makes this
 * work without any real network interface or elevated privileges. */

#include <stdio.h>
#include <string.h>

#include "../support/test.h"
#include "support/fixtures.h"
#include "support/netutil.h"
#include "support/process.h"
#include "support/ssdpclient.h"

TEST(msearch_over_loopback_multicast_gets_a_unicast_reply)
{
    const char *root = ftest_mkdir();
    int port = ftest_alloc_port();

    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char reply[1024];
    int n = ssdp_msearch("upnp:rootdevice", 2000, reply, sizeof(reply));

    if (n <= 0)
    {
        tests_fail(__FILE__, __LINE__, "no SSDP reply received over lo");
        ASSERT_TRUE(mdlna_stop(&proc));
        return;
    }

    ASSERT_STR_CONTAINS(reply, "HTTP/1.1 200 OK");
    ASSERT_STR_CONTAINS(reply, "ST:");
    ASSERT_STR_CONTAINS(reply, "USN:");

    char expected_location[64];
    snprintf(expected_location, sizeof(expected_location),
             "LOCATION: http://127.0.0.1:%d/rootDesc.xml", port);
    ASSERT_STR_CONTAINS(reply, expected_location);
    ASSERT_INT_EQ(n, 1);

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(msearch_ssdp_all_gets_one_reply_per_advertised_type)
{
    const char *root = ftest_mkdir();
    int port = ftest_alloc_port();

    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char replies[8192];
    ASSERT_INT_EQ(ssdp_msearch("ssdp:all", 1000, replies, sizeof(replies)), 6);
    ASSERT_STR_CONTAINS(replies, "ST: urn:schemas-upnp-org:service:ContentDirectory:1");

    ASSERT_TRUE(mdlna_stop(&proc));
}

TEST(msearch_for_unknown_type_gets_no_reply)
{
    const char *root = ftest_mkdir();
    int port = ftest_alloc_port();

    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, NULL);

    char replies[1024];
    ASSERT_INT_EQ(ssdp_msearch("urn:schemas-upnp-org:device:Printer:1", 500,
                               replies, sizeof(replies)), 0);

    ASSERT_TRUE(mdlna_stop(&proc));
}
