#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${CUPUACU_BENCHMARK_BUILD_DIR:-${ROOT_DIR}/build-benchmark-linux}"
cd "$ROOT_DIR"
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCUPUACU_BUILD_BENCHMARKS=ON -DCUPUACU_BUILD_SDL_BENCHMARKS=ON \
    -DCUPUACU_ENABLE_COVERAGE=OFF -DCUPUACU_ENABLE_RTSAN_LIBS=OFF \
    -DFETCHCONTENT_CACHE_ROOT="${FETCHCONTENT_CACHE_ROOT:-}"
cmake --build "$BUILD_DIR" --target cupuacu-benchmarks cupuacu-benchmarks-metrics \
    cupuacu-benchmarks-sdl --parallel "${CUPUACU_BUILD_JOBS:-4}"
python3 scripts/test_benchmark_runner.py
export SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=software
export CUPUACU_SUPPRESS_PORTAUDIO_ERRORS=1
export ALSA_CONFIG_PATH="${ALSA_CONFIG_PATH:-${ROOT_DIR}/docker/integration/linux/alsa.conf}"
xvfb-run -a -s '-screen 0 1920x1080x24' \
    python3 scripts/run-benchmarks.py --build-dir "$BUILD_DIR" --suite all "$@"
