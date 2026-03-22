#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-integration-linux"
FORCE_CONFIGURE="${CUPUACU_FORCE_CMAKE_CONFIGURE:-0}"

export DISPLAY="${DISPLAY:-:99}"
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export SDL_RENDER_DRIVER="${SDL_RENDER_DRIVER:-software}"
export CUPUACU_SUPPRESS_PORTAUDIO_ERRORS="${CUPUACU_SUPPRESS_PORTAUDIO_ERRORS:-1}"

Xvfb "${DISPLAY}" -screen 0 1920x1080x24 >/tmp/cupuacu-xvfb.log 2>&1 &
XVFB_PID=$!
cleanup() {
    kill "${XVFB_PID}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

if [[ "${FORCE_CONFIGURE}" == "1" || ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    cmake -G Ninja -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
        -DCUPUACU_BUILD_INTEGRATION_TESTS=ON
fi
cmake --build "${BUILD_DIR}" --target cupuacu-tests-integration -j2
"${BUILD_DIR}/cupuacu-tests-integration"
