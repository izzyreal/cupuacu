#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-integration-linux"

export DISPLAY="${DISPLAY:-:99}"
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-x11}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
export SDL_RENDER_DRIVER="${SDL_RENDER_DRIVER:-software}"

Xvfb "${DISPLAY}" -screen 0 1920x1080x24 >/tmp/cupuacu-xvfb.log 2>&1 &
XVFB_PID=$!
cleanup() {
    kill "${XVFB_PID}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

cmake -G Ninja -B "${BUILD_DIR}" -DCUPUACU_BUILD_INTEGRATION_TESTS=ON
cmake --build "${BUILD_DIR}" --target cupuacu-tests-integration -j2
"${BUILD_DIR}/cupuacu-tests-integration"
