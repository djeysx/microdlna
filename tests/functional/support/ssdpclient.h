#pragma once
/* Minimal SSDP M-SEARCH client, routed over the loopback interface so it
 * can reach a microdlnad instance started with "-i lo" without any real
 * network interface.
 *
 * Why this works: getifaddr.c only excludes IFF_LOOPBACK when no -i was
 * given (getifaddr.c, the "!ifname && (flags & IFF_LOOPBACK)" check); with
 * "-i lo", microdlnad joins the SSDP multicast group on lo
 * (add_multicast_membership(), minissdp.c), and lo has no hardware
 * multicast filtering, so membership/delivery there is handled purely by
 * the local IP stack. M-SEARCH replies are sent back as plain unicast
 * datagrams (send_ssdp_response(), minissdp.c) straight to the requester's
 * address, so they are unaffected by the server's own notify socket having
 * IP_MULTICAST_LOOP disabled (that setting only affects its periodic
 * NOTIFY multicast sends). */

#include <stddef.h>

/* Sends an M-SEARCH (ST: ssdp:all) query to 239.255.255.250:1900, with
 * outgoing traffic forced over lo (IP_MULTICAST_IF set to 127.0.0.1,
 * mirroring what microdlnad's own notify socket does for its interface),
 * then waits up to timeout_ms for a unicast reply and copies it
 * (NUL-terminated) into reply_buf. Returns the reply length on success, or
 * -1 on timeout/error. */
int ssdp_msearch(int timeout_ms, char *reply_buf, size_t reply_cap);
