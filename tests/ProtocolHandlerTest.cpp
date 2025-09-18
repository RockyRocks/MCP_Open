#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "ProtocolHandler.h"

TEST(ProtocolHandlerValidation, ValidRequest){
    ProtocolHandler h;
    nlohmann::json req = { {"command","echo"}, {"payload",nlohmann::json::object()} };
    EXPECT_TRUE(h.validateRequest(req));
}

TEST(ProtocolHandlerValidation, InvalidRequest){
    ProtocolHandler h;
    nlohmann::json req = { {"payload",nlohmann::json::object()} };
    EXPECT_FALSE(h.validateRequest(req));
}

TEST(ProtocolHandlerResponse, ValidResponse){
    ProtocolHandler h;
    nlohmann::json res = { {"status","ok"} };
    std::string s = h.createResponse(res);
    EXPECT_NE(s.find("ok"), std::string::npos);
}

TEST(ProtocolHandlerResponse, InvalidResponse){
    ProtocolHandler h;
    nlohmann::json res = { {"msg","bad"} };
    std::string s = h.createResponse(res);
    EXPECT_NE(s.find("error"), std::string::npos);
}
