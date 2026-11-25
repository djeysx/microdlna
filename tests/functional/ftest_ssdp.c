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

    const char *extra_args[] = { "-i", "lo", NULL };
    struct mdlna_proc proc;
    mdlna_start(&proc, root, port, extra_args);

    char reply[1024];
    int n = ssdp_msearch(2000, reply, sizeof(reply));

    if (n < 0)
    {
        tests_fail(__FILE__, __LINE__, "no SSDP reply received over lo");
        mdlna_stop(&proc);
        return;
    }

    ASSERT_STR_CONTAINS(reply, "HTTP/1.1 200 OK");
    ASSERT_STR_CONTAINS(reply, "ST:");
    ASSERT_STR_CONTAINS(reply, "USN:");

    char expected_location[64];
    snprintf(expected_location, sizeof(expected_location),
             "LOCATION: http://127.0.0.1:%d/rootDesc.xml", port);
    ASSERT_STR_CONTAINS(reply, expected_location);

    mdlna_stop(&proc);
}
