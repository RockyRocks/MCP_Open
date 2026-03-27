#include <gtest/gtest.h>
#include <validation/InputSanitizer.h>

TEST(InputSanitizerTest, SanitizeControlChars) {
    std::string input = "hello\x01world\x02test";
    std::string result = InputSanitizer::SanitizeString(input);
    EXPECT_EQ(result, "helloworldtest");
}

TEST(InputSanitizerTest, SanitizePreservesNewlines) {
    std::string input = "hello\nworld\ttab";
    std::string result = InputSanitizer::SanitizeString(input);
    EXPECT_EQ(result, input);
}

TEST(InputSanitizerTest, SanitizeMaxLength) {
    std::string input = "abcdefghij";
    std::string result = InputSanitizer::SanitizeString(input, 5);
    EXPECT_EQ(result.size(), 5u);
    EXPECT_EQ(result, "abcde");
}

TEST(InputSanitizerTest, ValidJsonDepth) {
    nlohmann::json shallow = {{"a", {{"b", "c"}}}};
    EXPECT_TRUE(InputSanitizer::ValidateJsonDepth(shallow, 10));
}

TEST(InputSanitizerTest, ExceedJsonDepth) {
    // Build deeply nested JSON
    nlohmann::json deep = "leaf";
    for (int i = 0; i < 35; ++i) {
        deep = nlohmann::json{{"level", deep}};
    }
    EXPECT_FALSE(InputSanitizer::ValidateJsonDepth(deep, 32));
}

TEST(InputSanitizerTest, ValidPayloadSize) {
    EXPECT_TRUE(InputSanitizer::ValidatePayloadSize("small", 1024));
}

TEST(InputSanitizerTest, ExceedPayloadSize) {
    EXPECT_FALSE(InputSanitizer::ValidatePayloadSize("toolarge", 5));
}
