#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE_TAG="${IMAGE_TAG:-cupuacu-integration-linux}"
CONTAINER_WORKDIR="/work"
BUILD_SUBDIR="${BUILD_SUBDIR:-build/linux-amd64-coverage-clean}"
DIST_SUBDIR="${DIST_SUBDIR:-dist}"
FETCHCONTENT_DIR="${FETCHCONTENT_DIR:-$ROOT_DIR/.ciwi-fetchcontent}"
CCACHE_DIR="${CCACHE_DIR:-$ROOT_DIR/.ciwi-ccache}"
BUILD_DIR="${ROOT_DIR}/${BUILD_SUBDIR}"
DIST_DIR="${ROOT_DIR}/${DIST_SUBDIR}"
CONTAINER_BUILD_DIR="${CONTAINER_WORKDIR}/${BUILD_SUBDIR}"
CONTAINER_DIST_DIR="${CONTAINER_WORKDIR}/${DIST_SUBDIR}"

if ! command -v podman >/dev/null 2>&1; then
    echo "podman not found in PATH" >&2
    exit 1
fi

if ! podman machine info >/dev/null 2>&1; then
    echo "podman machine is not running; start it with 'podman machine start'" >&2
    exit 1
fi

mkdir -p "${DIST_DIR}" "${FETCHCONTENT_DIR}" "${CCACHE_DIR}"

podman build -t "${IMAGE_TAG}" -f "${ROOT_DIR}/docker/integration/linux/Dockerfile" "${ROOT_DIR}"

podman run --rm \
    --userns keep-id \
    -e HOME=/tmp \
    -e CCACHE_DIR=/ccache \
    -e CCACHE_BASEDIR="${CONTAINER_WORKDIR}" \
    -e CCACHE_NOHASHDIR=true \
    -e CCACHE_COMPILERCHECK=content \
    -v "${ROOT_DIR}:${CONTAINER_WORKDIR}" \
    -v "${FETCHCONTENT_DIR}:/ciwi-fetchcontent" \
    -v "${CCACHE_DIR}:/ccache" \
    -w "${CONTAINER_WORKDIR}" \
    "${IMAGE_TAG}" \
    /bin/bash -lc "rm -rf \"${CONTAINER_BUILD_DIR}\" && ccache --zero-stats && cmake -G Ninja -S . -B \"${CONTAINER_BUILD_DIR}\" -DCMAKE_BUILD_TYPE=Debug -DCUPUACU_BUILD_CCACHE_ENABLED=ON -DCUPUACU_ENABLE_COVERAGE=ON -DCUPUACU_BUILD_INTEGRATION_TESTS=ON -DFETCHCONTENT_CACHE_ROOT=/ciwi-fetchcontent && cmake --build \"${CONTAINER_BUILD_DIR}\" --target cupuacu-tests cupuacu-tests-integration -j2"

podman run --rm \
    --userns keep-id \
    -e HOME=/tmp \
    -e SDL_VIDEODRIVER=offscreen \
    -e CUPUACU_SUPPRESS_PORTAUDIO_ERRORS=1 \
    -v "${ROOT_DIR}:${CONTAINER_WORKDIR}" \
    -w "${CONTAINER_WORKDIR}" \
    "${IMAGE_TAG}" \
    /bin/bash -lc "\"${CONTAINER_BUILD_DIR}/cupuacu-tests\" --reporter JUnit --out \"${CONTAINER_DIST_DIR}/junit-unit.xml\""

podman run --rm \
    --userns keep-id \
    -e HOME=/tmp \
    -e DISPLAY=:99 \
    -e SDL_VIDEODRIVER=x11 \
    -e SDL_AUDIODRIVER=dummy \
    -e SDL_RENDER_DRIVER=software \
    -e CUPUACU_SUPPRESS_PORTAUDIO_ERRORS=1 \
    -v "${ROOT_DIR}:${CONTAINER_WORKDIR}" \
    -w "${CONTAINER_WORKDIR}" \
    "${IMAGE_TAG}" \
    /bin/bash -lc "Xvfb \"\$DISPLAY\" -screen 0 1920x1080x24 >/tmp/cupuacu-coverage-xvfb.log 2>&1 & XVFB_PID=\$!; trap 'kill \$XVFB_PID >/dev/null 2>&1 || true' EXIT; rm -f \"${CONTAINER_DIST_DIR}/coverage.raw.info\" \"${CONTAINER_DIST_DIR}/coverage.info\" && rm -rf \"${CONTAINER_DIST_DIR}/coverage-html\" && \"${CONTAINER_BUILD_DIR}/cupuacu-tests-integration\" --reporter JUnit --out \"${CONTAINER_DIST_DIR}/junit-integration.xml\" && lcov --capture --rc geninfo_unexecuted_blocks=1 --ignore-errors source --directory \"${CONTAINER_BUILD_DIR}\" --base-directory \"${CONTAINER_WORKDIR}\" --output-file \"${CONTAINER_DIST_DIR}/coverage.raw.info\" && lcov --extract \"${CONTAINER_DIST_DIR}/coverage.raw.info\" '*/src/main/*' --output-file \"${CONTAINER_DIST_DIR}/coverage.info\" && perl -pe 's{^SF:.*/src/main/}{SF:src/main/}' \"${CONTAINER_DIST_DIR}/coverage.info\" > \"${CONTAINER_DIST_DIR}/coverage.info.tmp\" && mv \"${CONTAINER_DIST_DIR}/coverage.info.tmp\" \"${CONTAINER_DIST_DIR}/coverage.info\" && genhtml \"${CONTAINER_DIST_DIR}/coverage.info\" --output-directory \"${CONTAINER_DIST_DIR}/coverage-html\""

podman run --rm \
    --userns keep-id \
    -e HOME=/tmp \
    -e CCACHE_DIR=/ccache \
    -v "${CCACHE_DIR}:/ccache" \
    "${IMAGE_TAG}" \
    /bin/bash -lc "ccache --show-stats"

echo
echo "Coverage summary:"
podman run --rm \
    --userns keep-id \
    -e HOME=/tmp \
    -v "${ROOT_DIR}:${CONTAINER_WORKDIR}" \
    -w "${CONTAINER_WORKDIR}" \
    "${IMAGE_TAG}" \
    /bin/bash -lc "lcov --summary \"${CONTAINER_DIST_DIR}/coverage.info\""

echo
echo "Joint coverage report generated:"
echo "  LCOV: ${DIST_DIR}/coverage.info"
echo "  HTML: ${DIST_DIR}/coverage-html/index.html"
echo "  Unit JUnit: ${DIST_DIR}/junit-unit.xml"
echo "  Integration JUnit: ${DIST_DIR}/junit-integration.xml"
