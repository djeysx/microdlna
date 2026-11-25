/* Tests for upnpsoap.c's XML escaping helpers. upnpsoap.o is excluded from
 * this binary's link. */

#include "../upnpsoap.c"

#include "test.h"

TEST(xml_escape_double_returns_input_when_nothing_to_escape)
{
    const char *in = "plain text 123";
    const char *out = xml_escape_double(in);
    ASSERT_PTR_EQ(out, in);
}

TEST(xml_escape_double_returns_input_for_empty_string)
{
    const char *in = "";
    const char *out = xml_escape_double(in);
    ASSERT_PTR_EQ(out, in);
}

TEST(xml_escape_double_escapes_all_five_entities)
{
    const char *out = xml_escape_double("&<>\"'");
    ASSERT_STR_EQ(out, "&amp;amp;&amp;lt;&amp;gt;&amp;quot;&amp;apos;");
    free((void *)out);
}

TEST(xml_escape_double_mixed_text)
{
    const char *out = xml_escape_double("a&b<c");
    ASSERT_STR_EQ(out, "a&amp;amp;b&amp;lt;c");
    free((void *)out);
}

TEST(xml_unescape_all_five_entities_in_place)
{
    char tag[] = "&amp;&lt;&gt;&quot;&apos;";
    xml_unescape(tag);
    ASSERT_STR_EQ(tag, "&<>\"'");
}

TEST(xml_unescape_unknown_entity_is_kept_as_is)
{
    char tag[] = "&foo;";
    xml_unescape(tag);
    ASSERT_STR_EQ(tag, "&foo;");
}

TEST(xml_unescape_does_not_recurse_into_its_own_output)
{
    /* unescaping "&amp;lt;" yields '&' for the "&amp;" part, then the
     * remaining "lt;" is copied as plain characters (not re-unescaped as
     * "&lt;" would be), all in a single left-to-right pass. */
    char tag[] = "&amp;lt;";
    xml_unescape(tag);
    ASSERT_STR_EQ(tag, "&lt;");
}

TEST(xml_unescape_empty_string)
{
    char tag[] = "";
    xml_unescape(tag);
    ASSERT_STR_EQ(tag, "");
}
