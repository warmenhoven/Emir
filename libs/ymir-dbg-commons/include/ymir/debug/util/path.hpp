#pragma once

#include <string>
#include <vector>
#include <sstream>

namespace ymir::debug::util {

// Define the platform-specific search path delimiter
#ifdef _WIN32
inline constexpr char kSearchPathDelimiter = ';';
#else
inline constexpr char kSearchPathDelimiter = ':';
#endif

/// @brief Splits a search path string (like PATH) into individual directory segments.
/// @param pathStr The full path string to split.
/// @return A vector of non-empty directory strings.
inline std::vector<std::string> SplitSearchPath(const std::string& pathStr) {
    std::vector<std::string> result;
    std::stringstream ss(pathStr);
    std::string item;
    
    // Iterate through the string, splitting by the platform-specific delimiter
    while (std::getline(ss, item, kSearchPathDelimiter)) {
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    
    return result;
}

} // namespace ymir::debug::util
