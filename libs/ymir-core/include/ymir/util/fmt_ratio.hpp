#pragma once

#include "ratio.hpp"

#include <fmt/format.h>

template <>
struct fmt::formatter<Ratio> : fmt::formatter<std::string_view> {
    auto format(const Ratio &ratio, fmt::format_context &ctx) const {
        return fmt::format_to(ctx.out(), "{}:{}", ratio.Numerator(), ratio.Denominator());
    }
};
