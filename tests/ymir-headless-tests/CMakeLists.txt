add_executable(ymir-headless-tests
    src/config_tests.cpp
    src/config_parser_tests.cpp
    src/smoke_tests.cpp
    src/protocol_tests.cpp
)
add_executable(ymir::ymir-headless-tests ALIAS ymir-headless-tests)
set_target_properties(ymir-headless-tests PROPERTIES
                      VERSION ${Ymir_VERSION}
                      SOVERSION ${Ymir_VERSION_MAJOR})
target_compile_features(ymir-headless-tests PUBLIC cxx_std_20)

target_include_directories(ymir-headless-tests PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/apps/ymir-headless/src>"
)

find_package(Catch2 CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

target_link_libraries(ymir-headless-tests PRIVATE
    Catch2::Catch2WithMain
    ymir::ymir-dbg-commons
    tomlplusplus::tomlplusplus
)
# nlohmann_json is provided transitively via ymir-dbg-commons.

cmrk_copy_runtime_dlls(ymir-headless-tests)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    message(STATUS "Enabling IPO / LTO for ymir-headless-tests")
    set_property(TARGET ymir-headless-tests PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if (MSVC)
    vs_set_filters(TARGET ymir-headless-tests)
    set_target_properties(ymir-headless-tests PROPERTIES FOLDER "Ymir-tests")
endif ()

include(Catch)
catch_discover_tests(ymir-headless-tests)
