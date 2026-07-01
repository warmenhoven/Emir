#include <catch2/catch_test_macros.hpp>

#include <config.hpp>

TEST_CASE("HeadlessConfig defaults to an unset IPL boot configuration", "[config]") {
    ymir::debug::HeadlessConfig config;

    CHECK(config.ipl_path.empty());
    CHECK_FALSE(config.game_path.has_value());
    CHECK_FALSE(config.bram_path.has_value());
    CHECK(config.slave_enabled);
}

TEST_CASE("HeadlessConfig stores assigned paths and hardware flags", "[config]") {
    ymir::debug::HeadlessConfig config;
    config.ipl_path = "bios.bin";
    config.game_path = "game.cue";
    config.bram_path = "backup.ram";
    config.slave_enabled = false;

    CHECK(config.ipl_path == std::filesystem::path{"bios.bin"});
    REQUIRE(config.game_path.has_value());
    CHECK(*config.game_path == std::filesystem::path{"game.cue"});
    REQUIRE(config.bram_path.has_value());
    CHECK(*config.bram_path == std::filesystem::path{"backup.ram"});
    CHECK_FALSE(config.slave_enabled);
}
