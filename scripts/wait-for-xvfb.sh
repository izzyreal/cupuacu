#!/usr/bin/env bash
set -euo pipefail

XVFB_LOG_PATH="${1:-/tmp/cupuacu-xvfb.log}"

for _ in {1..100}; do
    if xdpyinfo -display "${DISPLAY:?DISPLAY must be set}" >/dev/null 2>&1; then
        exit 0
    fi
    sleep 0.05
done

echo "Xvfb did not become ready on ${DISPLAY}" >&2
if [[ -f "${XVFB_LOG_PATH}" ]]; then
    echo "Xvfb log:" >&2
    sed 's/^/  /' "${XVFB_LOG_PATH}" >&2
fi
exit 1
