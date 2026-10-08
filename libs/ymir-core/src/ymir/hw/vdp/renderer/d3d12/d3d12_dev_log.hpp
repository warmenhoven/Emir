#pragma once

namespace ymir::vdp {

namespace grp {

    // -------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // dx12_base
    //   dx12_upload
    //   dx12_vdp1
    //   dx12_vdp2

    struct dx12_base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "VDP-DX12";
    };

    struct dx12_upload : public dx12_base {
        // static constexpr devlog::Level level = devlog::level::trace;
        static constexpr std::string_view name = "VDP-DX12-Upload";
    };

    struct dx12_vdp1 : public dx12_base {
        static constexpr std::string_view name = "VDP1-DX12";
    };

    struct dx12_vdp2 : public dx12_base {
        static constexpr std::string_view name = "VDP2-DX12";
    };

} // namespace grp

} // namespace ymir::vdp