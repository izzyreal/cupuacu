FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    ninja-build \
    pkg-config \
    git \
    perl \
    python3 \
    xvfb \
    xauth \
    x11-apps \
    fonts-dejavu-core \
    libasound2-dev \
    libpulse-dev \
    libx11-dev \
    libxext-dev \
    libxrandr-dev \
    libxi-dev \
    libxcursor-dev \
    libxfixes-dev \
    libxrender-dev \
    libxinerama-dev \
    libxss-dev \
    libgl1-mesa-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

ENV DISPLAY=:99
ENV SDL_VIDEODRIVER=x11
ENV SDL_AUDIODRIVER=dummy
ENV SDL_RENDER_DRIVER=software
