#pragma once

#include "gfx_types.hpp"

#include <vector>

namespace app::gfx {

/// @brief Manages texture IDs within a graphics context.
class TextureIDManager {
public:
    /// @brief Retrieves the next free texture ID.
    /// @return a free texture ID
    TextureID GetNextTextureID();

    /// @brief Releases the given texture ID for reuse.
    /// @param[in] id the texture ID to free
    void FreeTextureID(TextureID id);

private:
    std::vector<TextureID> m_freeTextureIDs;
    TextureID m_nextTextureID = 0;
};

} // namespace app::gfx
