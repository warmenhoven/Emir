#pragma once

#include <ymir/core/types.hpp>

#include <algorithm>
#include <limits>
#include <numeric>
#include <utility>

/// @brief Immutable ratio of two integers.
class Ratio {
public:
    constexpr Ratio(uint32 num = 1, uint32 den = 1) {
        if (den == 0) {
            den = 1;
        }

        // Minimize fraction
        const uint32 gcd = std::gcd(num, den);
        m_num = num / gcd;
        m_den = den / gcd;
    }

    /// @brief Returns a ratio of 1.0.
    /// @return the ratio 1/1
    static constexpr Ratio One() {
        return {};
    }

    /// @brief Creates a ratio with the best approximation of the given `float` value.
    /// @param[in] value the value to convert into a ratio
    /// @return a ratio with an approximation of the given value
    static constexpr Ratio FromFloat(float value) {
        return FromFloatingPoint(value);
    }

    /// @brief Creates a ratio with the best approximation of the given `double` value.
    /// @param[in] value the value to convert into a ratio
    /// @return a ratio with an approximation of the given value
    static constexpr Ratio FromDouble(double value) {
        return FromFloatingPoint(value);
    }

    /// @brief Creates a ratio from the given percentage.
    /// @param[in] percentage the percentage
    /// @return a ratio representing the given percentage
    static constexpr Ratio FromPercentage(uint32 percentage) {
        return Ratio(percentage, 100u);
    }

    /// @brief Retrieves the numerator of this ratio.
    /// @return the numerator
    constexpr uint32 Numerator() const {
        return m_num;
    }

    /// @brief Retrieves the denominator of this ratio.
    /// @return the denominator
    constexpr uint32 Denominator() const {
        return m_den;
    }

    /// @brief Returns the tuple {numerator, denominator}.
    /// @return a `std::pair` with the numerator and denominator
    constexpr std::pair<uint32, uint32> Pair() const {
        return {m_num, m_den};
    }

    /// @brief Converts this ratio to a `float` value.
    /// @return this ratio as a float
    constexpr float AsFloat() const {
        return static_cast<float>(m_num) / m_den;
    }

    /// @brief Converts this ratio to a `double` value.
    /// @return this ratio as a double
    constexpr double AsDouble() const {
        return static_cast<double>(m_num) / m_den;
    }

    /// @brief Returns the inverse of this ratio.
    /// The inverse of zero results in zero as there's no representation of an undefined value with this class.
    /// @return a new ratio from this ratio's inverse
    constexpr Ratio Inverse() const {
        if (m_num == 0u) {
            return {0u, 1u};
        }
        return {m_den, m_num};
    }

    /// @brief Multiplies the given value by this ratio, rounding down.
    /// @param[in] value the value to multiply
    /// @return `floor(value * ratio)`
    constexpr uint32 MulFloor(uint32 value) const {
        return static_cast<uint32>(static_cast<uint64>(value) * m_num / m_den);
    }

    /// @brief Multiplies the given value by this ratio, rounding to the nearest integer.
    /// @param[in] value the value to multiply
    /// @return `round(value * ratio)`
    constexpr uint32 MulRound(uint32 value) const {
        return static_cast<uint32>((static_cast<uint64>(value) * m_num + m_den / 2) / m_den);
    }

    /// @brief Multiplies the given value by this ratio, rounding up.
    /// @param[in] value the value to multiply
    /// @return `ceil(value * ratio)`
    constexpr uint32 MulCeil(uint32 value) const {
        return static_cast<uint32>((static_cast<uint64>(value) * m_num + m_den - 1) / m_den);
    }

    friend constexpr bool operator<(const Ratio &lhs, const Ratio &rhs) {
        return static_cast<uint64>(lhs.m_num) * rhs.m_den < static_cast<uint64>(rhs.m_num) * lhs.m_den;
    }

    friend constexpr bool operator==(const Ratio &lhs, const Ratio &rhs) {
        return lhs.m_num == rhs.m_num && lhs.m_den == rhs.m_den;
    }

private:
    uint32 m_num;
    uint32 m_den;

    /// @brief Creates a ratio with the best approximation of the given floating point value.
    /// Special cases:
    /// - Negative values and zero return {0, 1}
    /// - Infinity and NaN return {0, 1}
    /// - Values too large to fit in `uint32` are saturated to {2^32-1, 1}
    ///
    /// @tparam T the floating point type
    /// @param[in] value the value to convert
    /// @return a ratio with the best approximation of the given value
    template <std::floating_point T>
    static constexpr Ratio FromFloatingPoint(T value) {
        constexpr uint64 kMax = std::numeric_limits<uint32>::max();

        // Handle special cases
        if (value <= static_cast<T>(0)) {
            // Zero, negative, negative infinity or NaN
            return Ratio{0u, 1u};
        }
        if (value >= static_cast<T>(kMax)) {
            // `uint32` overflow or positive infinity
            return Ratio{static_cast<uint32>(kMax), 1u};
        }

        // Continued fraction expansion using convergents and intermediate fractions
        // https://en.wikipedia.org/wiki/Simple_continued_fraction
        // A continued fraction has the form:
        //   a0 + 1/(a1 + 1/(a2 + 1/(a3 + ...)))
        // where a_i are integers, called "coefficients" or "terms"
        // Closely related to the Stern-Brocot tree, where we perform a binary search tree to approximate the number:
        // https://en.wikipedia.org/wiki/Stern%E2%80%93Brocot_tree
        const double dValue = static_cast<double>(value);
        auto absDiff = [](double a, double b) { return a < b ? b - a : a - b; };
        auto approx = [](uint64 h, uint64 k) { return static_cast<double>(h) / static_cast<double>(k); };

        // Convergents h/k
        uint64 h0 = 0, k0 = 1; // second previous
        uint64 h1 = 1, k1 = 0; // previous

        double x = dValue;
        for (int i = 0; i < 64; ++i) {
            const bool capped = x >= static_cast<double>(kMax);
            const uint64 a = capped ? kMax : static_cast<uint64>(x);

            // Compute next convergent
            const uint64 h2 = a * h1 + h0;
            const uint64 k2 = a * k1 + k0;

            if (h2 > kMax || k2 > kMax) {
                // The next convergent doesn't fit.
                // Try the best semiconvergent sh/sk = (h0 + t*h1) / (k0 + t*k1) with the largest t that fits.
                uint64 t = kMax;
                if (h1 != 0) {
                    t = std::min(t, (kMax - h0) / h1);
                }
                if (k1 != 0) {
                    t = std::min(t, (kMax - k0) / k1);
                }
                if (t > 0 && k1 != 0) {
                    const uint64 sh = h0 + t * h1;
                    const uint64 sk = k0 + t * k1;
                    if (absDiff(approx(sh, sk), dValue) < absDiff(approx(h1, k1), dValue)) {
                        h1 = sh;
                        k1 = sk;
                    }
                }
                break;
            }

            // Push window forward (second previous <- previous <- current)
            h0 = h1;
            k0 = k1;
            h1 = h2;
            k1 = k2;

            if (capped) {
                // The value overflows the limits of an `uint32`
                break;
            }
            if (static_cast<T>(approx(h1, k1)) == dValue) {
                // The fraction converts back to the exact input value
                break;
            }

            // Extract the next term.
            // We have:
            //   a_i + 1/(a_{i+1} + ...)
            // a_i is an integer. Subtract it to get the rest of the continued fraction:
            //   1/(a_{i+1} + ...)
            // Invert this to get the next term:
            //   a_{i+1} + 1/(a_{i+2} + ...)

            // Get the fractional part
            const double frac = x - static_cast<double>(a);
            if (frac <= 0.0) {
                // No fractional part to expand; we're done
                break;
            }
            // Invert for the next iteration
            x = 1.0 / frac;
        }

        return Ratio{static_cast<uint32>(h1), static_cast<uint32>(k1)};
    }
};
