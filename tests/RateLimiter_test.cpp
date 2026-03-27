#include <gtest/gtest.h>
#include <security/RateLimiter.h>

TEST(RateLimiterTest, AllowWithinLimit) {
    RateLimiter limiter(10);
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.AllowRequest("192.168.1.1"));
    }
}

TEST(RateLimiterTest, DenyOverLimit) {
    RateLimiter limiter(3);
    EXPECT_TRUE(limiter.AllowRequest("192.168.1.1"));
    EXPECT_TRUE(limiter.AllowRequest("192.168.1.1"));
    EXPECT_TRUE(limiter.AllowRequest("192.168.1.1"));
    EXPECT_FALSE(limiter.AllowRequest("192.168.1.1"));
}

TEST(RateLimiterTest, DifferentIpsIndependent) {
    RateLimiter limiter(1);
    EXPECT_TRUE(limiter.AllowRequest("192.168.1.1"));
    EXPECT_TRUE(limiter.AllowRequest("192.168.1.2"));
    EXPECT_FALSE(limiter.AllowRequest("192.168.1.1"));
}

TEST(RateLimiterTest, Reset) {
    RateLimiter limiter(1);
    EXPECT_TRUE(limiter.AllowRequest("192.168.1.1"));
    EXPECT_FALSE(limiter.AllowRequest("192.168.1.1"));
    limiter.Reset();
    EXPECT_TRUE(limiter.AllowRequest("192.168.1.1"));
}
