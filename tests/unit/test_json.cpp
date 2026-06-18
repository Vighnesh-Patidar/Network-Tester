#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

TEST(Json, ParsesScalarsAndObjects) {
    const auto doc = nlohmann::json::parse(R"({"a": 1, "b": 2.5, "c": "x", "d": true})");
    EXPECT_EQ(doc.at("a").get<int>(), 1);
    EXPECT_NEAR(doc.at("b").get<double>(), 2.5, 1e-9);
    EXPECT_EQ(doc.at("c").get<std::string>(), std::string("x"));
    EXPECT_TRUE(doc.at("d").get<bool>());
}

TEST(Json, ContainsAndValueDefaults) {
    const auto doc = nlohmann::json::parse(R"({"present": 42})");
    EXPECT_TRUE(doc.contains("present"));
    EXPECT_FALSE(doc.contains("absent"));
    EXPECT_EQ(doc.value("present", 0), 42);
    EXPECT_EQ(doc.value("absent", 7), 7);
    EXPECT_EQ(doc.value("absent", "fallback"), std::string("fallback"));
}

TEST(Json, ParsesNestedArrays) {
    const auto doc = nlohmann::json::parse(R"({"items": [{"id": "r1"}, {"id": "r2"}]})");
    int count = 0;
    for (const auto& item : doc.at("items")) {
        EXPECT_FALSE(item.at("id").get<std::string>().empty());
        ++count;
    }
    EXPECT_EQ(count, 2);
}

TEST(Json, RoundTripsThroughDump) {
    nlohmann::json built;
    built["nodes"] = nlohmann::json::array();
    nlohmann::json node;
    node["id"] = "r1";
    node["metric"] = 10;
    node["loss"] = 0.0;
    built["nodes"].push_back(node);

    const std::string serialized = built.dump();
    const auto reparsed = nlohmann::json::parse(serialized);
    EXPECT_EQ(reparsed.at("nodes").size(), 1u);
    EXPECT_EQ(reparsed.at("nodes").at(0).at("id").get<std::string>(), std::string("r1"));
    EXPECT_EQ(reparsed.at("nodes").at(0).at("metric").get<int>(), 10);
}

TEST(Json, FloatsKeepDecimalPoint) {
    nlohmann::json j;
    j["loss"] = 0.0;
    const std::string s = j.dump();
    // A zero float must not serialize as an integer literal, or a downstream
    // strict reader could mistype the field.
    EXPECT_NE(s.find("0.0"), std::string::npos);
}

TEST(Json, EscapesStrings) {
    nlohmann::json j;
    j["k"] = std::string("line1\nline2\t\"q\"");
    const std::string s = j.dump();
    const auto back = nlohmann::json::parse(s);
    EXPECT_EQ(back.at("k").get<std::string>(), std::string("line1\nline2\t\"q\""));
}

TEST(Json, ThrowsOnMissingKey) {
    const auto doc = nlohmann::json::parse(R"({"a": 1})");
    EXPECT_THROW(doc.at("missing"), std::exception);
}
