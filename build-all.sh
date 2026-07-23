#!/usr/bin/env bash
#
# build-all.sh - Build ymir libretro core for all platforms
#
# Platforms built (in order):
#   Desktop:  Windows x64, Linux x64, macOS x64, macOS ARM64
#   Cellular: Android ARM64, Android x64, iOS ARM64
#   Console:  tvOS ARM64
#
# Windows, Linux, and Android builds run inside Docker containers.
# macOS, iOS, and tvOS builds run natively (requires Xcode).
#
set -euo pipefail

CORENAME=ymir
EXTRA_PATH=apps/ymir-libretro
CMAKE_SOURCE_ROOT=.
CORE_BASE_ARGS="-DYmir_ENABLE_LIBRETRO=ON -DYmir_DEV_BUILD=OFF"

CI_REGISTRY="git.libretro.com:5050"

# Docker images (from CI templates, with overrides from .gitlab-ci.yml)
IMG_WINDOWS="${CI_REGISTRY}/libretro-infrastructure/libretro-build-mxe-win-cross-cores:gcc12"
IMG_LINUX="${CI_REGISTRY}/libretro-infrastructure/libretro-build-amd64-ubuntu:backports"
IMG_ANDROID="${CI_REGISTRY}/libretro-infrastructure/libretro-build-android:latest"

# Android NDK path inside the Docker container
NDK_ROOT_DOCKER="/android-sdk-linux/ndk/26.2.11394342"

# Host-side parallelism
NUMPROC=$(($(nproc || sysctl -n hw.ncpu || echo 4)/1))
[ "$NUMPROC" -lt 1 ] && NUMPROC=1

# Absolute path to source (Docker needs absolute mount paths)
SRC_DIR="$(pwd)"

cleanup() {
  rm -rf "$BUILD_DIR"
}

########################################
# Helper: run cmake build natively (Apple)
########################################
build_native() {
  local label="$1" libname="$2" build_dir="$3" build_type="$4"
  shift 4
  local extra_args=("$@")

  BUILD_DIR="build/${build_dir}"
  echo "============================================"
  echo "Building: ${label}"
  echo "============================================"

  cmake \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    ${CORE_BASE_ARGS} \
    "${extra_args[@]}" \
    "${CMAKE_SOURCE_ROOT}" \
    -B"${BUILD_DIR}"

  cmake --build "${BUILD_DIR}" --target "${CORENAME}_libretro" --config "${build_type}" -- -j "${NUMPROC}"

  # mv "${BUILD_DIR}/${EXTRA_PATH}/${libname}" "${libname}"
  # cleanup
  echo "  -> ${libname}"
  echo ""
}

########################################
# Desktop: Windows x64 (Docker)
########################################
build_windows_x64() {
  local libname="${CORENAME}_libretro.dll"
  local build_dir="build/windows-x86_64"

  docker run --rm \
    -v "${SRC_DIR}:${SRC_DIR}" \
    -w "${SRC_DIR}" \
    "${IMG_WINDOWS}" \
    bash -euo pipefail -c "
      export NUMPROC=\$((\$(nproc)/1)); [ \$NUMPROC -lt 1 ] && NUMPROC=1
      if builtin type -P ccache &>/dev/null; then ccache -V &>/dev/null; fi

      x86_64-w64-mingw32.clang-cmake \
        -DCMAKE_BUILD_TYPE=Release \
        ${CORE_BASE_ARGS} \
        -DYmir_AVX2=ON \
        . -B${build_dir}

      x86_64-w64-mingw32.clang-cmake --build ${build_dir} \
        --target ${CORENAME}_libretro --config Release -- -j \$NUMPROC
    "

  echo "  -> ${libname}"
  echo ""
}

########################################
# Desktop: Linux x64 (Docker)
########################################
build_linux_x64() {
  local libname="${CORENAME}_libretro.so"
  local build_dir="build/linux-x86_64"

  docker run --rm \
    -v "${SRC_DIR}:${SRC_DIR}" \
    -w "${SRC_DIR}" \
    "${IMG_LINUX}" \
    bash -euo pipefail -c "
      export NUMPROC=\$((\$(nproc)/1)); [ \$NUMPROC -lt 1 ] && NUMPROC=1
      export CC=/usr/bin/gcc-12
      export CXX=/usr/bin/g++-12
      if builtin type -P ccache &>/dev/null; then ccache -V &>/dev/null; fi

      cmake \
        -DCMAKE_BUILD_TYPE=Release \
        ${CORE_BASE_ARGS} \
        -DYmir_AVX2=ON \
        . -B${build_dir}

      cmake --build ${build_dir} \
        --target ${CORENAME}_libretro --config Release -- -j \$NUMPROC
    "

  echo "  -> ${libname}"
  echo ""
}

########################################
# Desktop: macOS x64 (native)
########################################
build_osx_x64() {
  local libname="${CORENAME}_libretro.dylib"

  export MACOSX_DEPLOYMENT_TARGET="11.0"

  build_native "osx-x64" "$libname" "osx-x86_64" "Release" \
    -DYmir_AVX2=ON \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0"
}

########################################
# Desktop: macOS ARM64 (native)
########################################
build_osx_arm64() {
  local libname="${CORENAME}_libretro.dylib"

  export MACOSX_DEPLOYMENT_TARGET="11.0"

  build_native "osx-arm64" "$libname" "osx-arm64" "Release" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_APPLE_SILICON_PROCESSOR=arm64 \
    -DCMAKE_SYSTEM_PROCESSOR=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0"
}

########################################
# Cellular: Android ARM64 (Docker)
########################################
build_android_arm64() {
  local libname="${CORENAME}_libretro_android.so"
  local abi="arm64-v8a"
  local api_level="16"
  local build_dir="build/android-${abi}"

  docker run --rm \
    -v "${SRC_DIR}:${SRC_DIR}" \
    -w "${SRC_DIR}" \
    "${IMG_ANDROID}" \
    bash -euo pipefail -c "
      export NUMPROC=\$((\$(nproc)/1)); [ \$NUMPROC -lt 1 ] && NUMPROC=1
      export NDK_ROOT=${NDK_ROOT_DOCKER}

      cmake \
        -DCMAKE_BUILD_TYPE=Release \
        ${CORE_BASE_ARGS} \
        -DANDROID_PLATFORM=android-${api_level} \
        -DCMAKE_TOOLCHAIN_FILE=\${NDK_ROOT}/build/cmake/android.toolchain.cmake \
        -DANDROID_STL=c++_static \
        -DANDROID_ABI=${abi} \
        . -B${build_dir}

      cmake --build ${build_dir} \
        --target ${CORENAME}_libretro --config Release -- -j \$NUMPROC
    "

  echo "  -> ${libname}"
  echo ""
}

########################################
# Cellular: Android x64 (Docker)
########################################
build_android_x64() {
  local libname="${CORENAME}_libretro_android.so"
  local abi="x86_64"
  local api_level="16"
  local build_dir="build/android-${abi}"

  docker run --rm \
    -v "${SRC_DIR}:${SRC_DIR}" \
    -w "${SRC_DIR}" \
    "${IMG_ANDROID}" \
    bash -euo pipefail -c "
      export NUMPROC=\$((\$(nproc)/1)); [ \$NUMPROC -lt 1 ] && NUMPROC=1
      export NDK_ROOT=${NDK_ROOT_DOCKER}

      cmake \
        -DCMAKE_BUILD_TYPE=Release \
        ${CORE_BASE_ARGS} \
        -DYmir_AVX2=ON \
        -DANDROID_PLATFORM=android-${api_level} \
        -DCMAKE_TOOLCHAIN_FILE=\${NDK_ROOT}/build/cmake/android.toolchain.cmake \
        -DANDROID_STL=c++_static \
        -DANDROID_ABI=${abi} \
        . -B${build_dir}

      cmake --build ${build_dir} \
        --target ${CORENAME}_libretro --config Release -- -j \$NUMPROC
    "

  echo "  -> ${libname}"
  echo ""
}

########################################
# Cellular: iOS ARM64 (native)
########################################
build_ios_arm64() {
  local libname="${CORENAME}_libretro.dylib"
  local arch="arm64"
  local minver="14.0"

  BUILD_DIR="build/ios-${arch}"
  echo "============================================"
  echo "Building: ios-${arch}"
  echo "============================================"

  cmake \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_C_FLAGS="-DIOS" \
    -DCMAKE_CXX_FLAGS="-DIOS" \
    -DIOS=ON \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${minver}" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_SYSTEM_PROCESSOR="${arch}" \
    -DCMAKE_OSX_ARCHITECTURES="${arch}" \
    ${CORE_BASE_ARGS} \
    "${CMAKE_SOURCE_ROOT}" \
    -B"${BUILD_DIR}"

  cmake --build "${BUILD_DIR}" --target "${CORENAME}_libretro" --config RelWithDebInfo -- -j "${NUMPROC}"

  # mv "${BUILD_DIR}/${EXTRA_PATH}/${libname}" "${libname}"

  # # Generate dSYM and strip
  # dsymutil "$libname" -o "${libname}.dSYM" 2>/dev/null || true
  # strip -S "$libname" 2>/dev/null || true
  # zip -r "${libname}.dSYM.zip" "${libname}.dSYM" 2>/dev/null || true

  # cleanup
  echo "  -> ${libname}"
  echo ""
}

########################################
# Console: tvOS ARM64 (native)
########################################
build_tvos_arm64() {
  local libname="${CORENAME}_libretro.dylib"
  local arch="arm64"
  local minver="14.0"

  BUILD_DIR="build/tvos-${arch}"
  echo "============================================"
  echo "Building: tvos-${arch}"
  echo "============================================"

  cmake \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_C_FLAGS="-DIOS" \
    -DCMAKE_CXX_FLAGS="-DIOS" \
    -DIOS=ON \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${minver}" \
    -DCMAKE_SYSTEM_NAME=tvOS \
    -DCMAKE_SYSTEM_PROCESSOR="${arch}" \
    -DCMAKE_OSX_ARCHITECTURES="${arch}" \
    ${CORE_BASE_ARGS} \
    "${CMAKE_SOURCE_ROOT}" \
    -B"${BUILD_DIR}"

  cmake --build "${BUILD_DIR}" --target "${CORENAME}_libretro" --config RelWithDebInfo -- -j "${NUMPROC}"

  # mv "${BUILD_DIR}/${EXTRA_PATH}/${libname}" "${libname}"

  # # Generate dSYM and strip
  # dsymutil "$libname" -o "${libname}.dSYM" 2>/dev/null || true
  # strip -S "$libname" 2>/dev/null || true
  # zip -r "${libname}.dSYM.zip" "${libname}.dSYM" 2>/dev/null || true

  # cleanup
  echo "  -> ${libname}"
  echo ""
}

########################################
# Main
########################################
main() {
  echo "Ymir libretro - Building all platforms"
  echo "NUMPROC=${NUMPROC}"
  echo ""

  # Ensure git submodules are initialized
  git submodule update --init --recursive

  # Desktop platforms
  build_windows_x64
  build_linux_x64
  build_osx_x64
  build_osx_arm64

  # Cellular platforms
  build_android_arm64
  build_android_x64
  build_ios_arm64

  # Console platforms
  build_tvos_arm64

  echo "============================================"
  echo "All builds complete."
  echo "============================================"
}

main "$@"
