find_package(nlohmann_json CONFIG REQUIRED)

add_library(ymir-dbg-commons INTERFACE)
add_library(ymir::ymir-dbg-commons ALIAS ymir-dbg-commons)

target_include_directories(ymir-dbg-commons
    INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_compile_features(ymir-dbg-commons INTERFACE cxx_std_20)

target_link_libraries(ymir-dbg-commons INTERFACE
    nlohmann_json::nlohmann_json
)
