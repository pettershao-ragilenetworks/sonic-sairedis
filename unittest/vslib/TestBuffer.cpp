#include "Buffer.h"

#include <gtest/gtest.h>

using namespace saivs;

TEST(Buffer, ctr)
{
    EXPECT_THROW(new Buffer(0,0), std::runtime_error);

    Buffer b((const uint8_t*)"foo", 3);
}

TEST(Buffer, dtr)
{
    auto b = std::make_shared<Buffer>((const uint8_t*)"foo", 3);

    b = nullptr;
}

TEST(Buffer, getData)
{
    EXPECT_THROW(new Buffer(0,0), std::runtime_error);

    Buffer b((const uint8_t*)"foo", 3);

    EXPECT_NE(b.getData(), nullptr);
}

TEST(Buffer, getSize)
{
    EXPECT_THROW(new Buffer(0,0), std::runtime_error);

    Buffer b((const uint8_t*)"foo", 3);

    EXPECT_EQ(b.getSize(), 3);
}

TEST(Buffer, flow)
{
    EXPECT_THROW(new Buffer(0,0), std::runtime_error);

    Buffer b((const uint8_t*)"foo", 3);

    EXPECT_NE(b.getData(), nullptr);

    EXPECT_EQ(b.getSize(), 3);
}
