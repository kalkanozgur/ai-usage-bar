#include <gtest/gtest.h>

#include "core/i18n.hpp"

using namespace cub::i18n;

TEST(I18n, ParsesLanguage) {
    EXPECT_EQ(parse_lang("tr"), Lang::Tr);
    EXPECT_EQ(parse_lang("TR"), Lang::Tr);
    EXPECT_EQ(parse_lang("en"), Lang::En);
    EXPECT_EQ(parse_lang(""), Lang::En);
    EXPECT_EQ(parse_lang("de"), Lang::En);
}

TEST(I18n, EveryKeyHasBothLanguages) {
    for (int i = 0; i < static_cast<int>(S::Count_); ++i) {
        const auto k = static_cast<S>(i);
        EXPECT_STRNE(t(Lang::En, k), "") << "missing en for key " << i;
        EXPECT_STRNE(t(Lang::Tr, k), "") << "missing tr for key " << i;
    }
}

TEST(I18n, Formats) {
    EXPECT_EQ(pct(Lang::En, 23.0), "23%");
    EXPECT_EQ(pct(Lang::Tr, 23.0), "%23");
    EXPECT_EQ(num(Lang::Tr, 3.04, 2), "3,04");
    EXPECT_EQ(num(Lang::En, 3.04, 2), "3.04");
    EXPECT_EQ(remaining(Lang::En, 2 * 3600 + 14 * 60), "2h14m");
    EXPECT_EQ(remaining(Lang::Tr, 2 * 3600 + 14 * 60), "2s14dk");
    EXPECT_STREQ(weekday(Lang::En, 5), "Fri");
    EXPECT_STREQ(month(Lang::Tr, 10), "Eki");
    EXPECT_EQ(tf(Lang::En, S::RatePoint, "0.12"), "1 session point \xE2\x89\x88 0.12 week points");
}
