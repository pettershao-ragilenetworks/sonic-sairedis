#include "SaiAttrWrap.h"

#include <gtest/gtest.h>

using namespace saivs;

TEST(SaiAttrWrap, ctr)
{
    EXPECT_THROW(std::make_shared<SaiAttrWrap>("attrId", "attrValue"), std::runtime_error);
}
