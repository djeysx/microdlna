/* Tests for getifaddr.c: validate_uuid() and set_interfaces_from_string().
 * getifaddr.o is excluded from this binary's link. */

#include "../getifaddr.c"

#include <stdio.h>

#include "test.h"

TEST(validate_uuid_accepts_well_formed_uuid)
{
    ASSERT_TRUE(validate_uuid("4d696e69-444c-164e-9d41-000000000000"));
}

TEST(validate_uuid_rejects_non_hex_character)
{
    /* same length as a valid UUID; 'Z' replaces a hex digit */
    ASSERT_TRUE(!validate_uuid("4d696e69-444Z-164e-9d41-000000000000"));
}

TEST(validate_uuid_rejects_misplaced_dash)
{
    /* same overall length; a dash moved one position to the left */
    ASSERT_TRUE(!validate_uuid("4d696e6-9444c-164e-9d41-000000000000"));
}

TEST(validate_uuid_rejects_trailing_garbage)
{
    /* an otherwise well-formed UUID with extra characters after it: the
     * function's final check reads one real (non-NUL) byte right after
     * the 36 expected characters, so this is safe without padding. */
    ASSERT_TRUE(!validate_uuid("4d696e69-444c-164e-9d41-000000000000-extra"));
}

TEST(validate_uuid_rejects_too_short_input)
{
    ASSERT_TRUE(!validate_uuid("1234"));
}

TEST(validate_uuid_rejects_empty_string)
{
    ASSERT_TRUE(!validate_uuid(""));
}

TEST(set_interfaces_from_string_trims_and_splits_on_comma)
{
    set_interfaces_from_string("eth0, wlan0");

    ASSERT_STR_EQ(ifaces[0], "eth0");
    ASSERT_STR_EQ(ifaces[1], "wlan0");
    ASSERT_NULL(ifaces[2]);

    free_ifaces();
}

TEST(set_interfaces_from_string_skips_empty_tokens)
{
    set_interfaces_from_string("eth0,,wlan0");

    ASSERT_STR_EQ(ifaces[0], "eth0");
    ASSERT_STR_EQ(ifaces[1], "wlan0");
    ASSERT_NULL(ifaces[2]);

    free_ifaces();
}

TEST(set_interfaces_from_string_trims_leading_whitespace_on_the_whole_input)
{
    set_interfaces_from_string("   eth0");

    ASSERT_STR_EQ(ifaces[0], "eth0");

    free_ifaces();
}

TEST(set_interfaces_from_string_truncates_past_max_lan_addr)
{
    /* MAX_LAN_ADDR is 4 */
    set_interfaces_from_string("a,b,c,d,e");

    ASSERT_STR_EQ(ifaces[0], "a");
    ASSERT_STR_EQ(ifaces[1], "b");
    ASSERT_STR_EQ(ifaces[2], "c");
    ASSERT_STR_EQ(ifaces[3], "d");

    free_ifaces();
}

TEST(free_ifaces_clears_all_slots)
{
    set_interfaces_from_string("eth0,wlan0");
    free_ifaces();

    for (int i = 0; i < MAX_LAN_ADDR; i++)
        ASSERT_NULL(ifaces[i]);
}
