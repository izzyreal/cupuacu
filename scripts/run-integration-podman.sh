#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE_TAG="${IMAGE_TAG:-cupuacu-integration-linux}"
CONTAINER_WORKDIR="/work"
HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

if ! command -v podman >/dev/null 2>&1; then
    echo "podman not found in PATH" >&2
    exit 1
fi

if ! podman machine info >/dev/null 2>&1; then
    echo "podman machine is not running; start it with 'podman machine start'" >&2
    exit 1
fi

podman build -t "${IMAGE_TAG}" -f "${ROOT_DIR}/docker/integration/linux/Dockerfile" "${ROOT_DIR}"

podman run --rm \
    --userns keep-id \
    -e HOME=/tmp \
    -e DISPLAY=:99 \
    -e SDL_VIDEODRIVER=x11 \
    -e SDL_AUDIODRIVER=dummy \
    -e SDL_RENDER_DRIVER=software \
    -v "${ROOT_DIR}:${CONTAINER_WORKDIR}" \
    -w "${CONTAINER_WORKDIR}" \
    "${IMAGE_TAG}" \
    /bin/bash -lc "chown -R ${HOST_UID}:${HOST_GID} build-integration-linux 2>/dev/null || true; /bin/bash ./scripts/run-integration-linux.sh"
