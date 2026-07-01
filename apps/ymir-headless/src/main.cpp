#include "config_parser.hpp"

#include <fmt/format.h>

int main(int argc, char **argv) {
    auto config = ymir::debug::LoadConfig(argc, argv);
    if (!ymir::debug::ValidateConfig(config)) {
        return 1;
    }

    fmt::print(stderr, "ymir-headless: ipl: {}\n", config.ipl_path.string());
    if (config.game_path) {
        fmt::print(stderr, "ymir-headless: game: {}\n", config.game_path->string());
    }
    if (config.bram_path) {
        fmt::print(stderr, "ymir-headless: bram: {}\n", config.bram_path->string());
    }
    fmt::print(stderr, "ymir-headless: slave: {}\n",
               config.slave_enabled ? "enabled" : "disabled");

    return 0;
}
