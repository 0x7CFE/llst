#include <gtest/gtest.h>
#include <types.h>

TEST(TInteger, ctor) {
    {
        SCOPED_TRACE("implicit from int");
        TInteger x(42);
        ASSERT_EQ(42, x.getValue());
    }
    {
        SCOPED_TRACE("implicit from unsigned int");
        TInteger x(43u);
        ASSERT_EQ(43, x.getValue());
    }
    {
        SCOPED_TRACE("assign int ctor");
        TInteger x = 44;
        ASSERT_EQ(44, x.getValue());
    }
    {
        SCOPED_TRACE("copy ctor");
        TInteger x(TInteger(45));
        ASSERT_EQ(45, x.getValue());
    }
    {
        SCOPED_TRACE("assign ctor");
        TInteger x = TInteger(46);
        ASSERT_EQ(46, x.getValue());
    }
}

TEST(TInteger, add) {
    {
        SCOPED_TRACE("+ int");
        TInteger x = 40;
        TInteger y = x + 2;
        ASSERT_EQ(42, y.getValue());
    }
    {
        SCOPED_TRACE("+ TInteger");
        TInteger x = 40;
        TInteger y = x + TInteger(3);
        ASSERT_EQ(43, y.getValue());
    }
    {
        SCOPED_TRACE("+= TInteger");
        TInteger x = 40;
        x += TInteger(4);
        ASSERT_EQ(44, x.getValue());
    }
    {
        SCOPED_TRACE("+= int");
        TInteger x = 40;
        x += 5;
        ASSERT_EQ(45, x.getValue());
    }
}
