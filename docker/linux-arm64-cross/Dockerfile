FROM dockcross/linux-arm64

RUN dpkg --add-architecture arm64 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        libx11-dev:arm64 \
        libxext-dev:arm64 \
        libxcursor-dev:arm64 \
        libxi-dev:arm64 \
        libxfixes-dev:arm64 \
        libxrandr-dev:arm64 \
        libxrender-dev:arm64 \
        libxss-dev:arm64 \
        libxinerama-dev:arm64 \
        libwayland-dev:arm64 \
        libxkbcommon-dev:arm64 \
        wayland-protocols \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*
