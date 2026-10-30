#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/json_lite.hpp"

namespace json = cub::json;

TEST(Json, MemberFindsDirectKeysOnly) {
    const std::string doc = R"({"a":{"x":1},"x":2})";
    const auto root = json::root(doc);
    ASSERT_TRUE(root);
    const auto x = json::member(doc, *root, "x");
    ASSERT_TRUE(x);
    EXPECT_EQ(json::as_number(doc, *x), 2.0);
}

TEST(Json, MemberSkipsNestedValuesAndStringsWithBraces) {
    const std::string doc = R"( { "s" : "}{,\"", "arr":[1,{"k":[2]}], "k" : true } )";
    const auto root = json::root(doc);
    ASSERT_TRUE(root);
    const auto k = json::member(doc, *root, "k");
    ASSERT_TRUE(k);
    EXPECT_EQ(doc.substr(k->begin, k->size()), "true");
    EXPECT_EQ(json::as_string(doc, *json::member(doc, *root, "s")), "}{,\"");
}

TEST(Json, MissingKeyAndNull) {
    const std::string doc = R"({"a":null})";
    const auto root = *json::root(doc);
    EXPECT_FALSE(json::member(doc, root, "b"));
    const auto a = json::member(doc, root, "a");
    ASSERT_TRUE(a);
    EXPECT_TRUE(json::is_null(doc, *a));
    EXPECT_FALSE(json::is_object(doc, *a));
}

TEST(Json, StringEscapes) {
    const std::string doc = R"(["a\nb", "çğ", "😀", "q\"\\/"])";
    std::vector<std::string> out;
    json::for_each_element(doc, *json::root(doc), [&](json::Span s) {
        out.push_back(json::as_string(doc, s).value_or("<err>"));
        return true;
    });
    ASSERT_EQ(out.size(), 4U);
    EXPECT_EQ(out[0], "a\nb");
    EXPECT_EQ(out[1], "\xC3\xA7\xC4\x9F");          // çğ
    EXPECT_EQ(out[2], "\xF0\x9F\x98\x80");          // 😀
    EXPECT_EQ(out[3], "q\"\\/");
}

TEST(Json, Numbers) {
    const std::string doc = R"([23.5, -1, 1e3, 1767000000000])";
    std::vector<double> out;
    json::for_each_element(doc, *json::root(doc), [&](json::Span s) {
        out.push_back(json::as_number(doc, s).value_or(-999));
        return true;
    });
    ASSERT_EQ(out.size(), 4U);
    EXPECT_DOUBLE_EQ(out[0], 23.5);
    EXPECT_DOUBLE_EQ(out[1], -1.0);
    EXPECT_DOUBLE_EQ(out[2], 1000.0);
    EXPECT_DOUBLE_EQ(out[3], 1767000000000.0);
}

TEST(Json, ForEachStopsEarly) {
    const std::string doc = "[1,2,3]";
    int seen = 0;
    json::for_each_element(doc, *json::root(doc), [&](json::Span) { return ++seen < 2; });
    EXPECT_EQ(seen, 2);
}

TEST(Json, MalformedDocuments) {
    EXPECT_FALSE(json::root(R"({"a":1)"));
    EXPECT_FALSE(json::root(R"("unterminated)"));
    EXPECT_FALSE(json::root(""));
}

TEST(Json, QuoteRoundTrip) {
    const std::string raw = "tab\tquote\"back\\slash\x01";
    const std::string quoted = json::quote(raw);
    EXPECT_EQ(json::as_string(quoted, *json::root(quoted)), raw);
}
