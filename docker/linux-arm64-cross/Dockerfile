FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        ccache \
        cmake \
        git \
        libasound2-dev \
        libegl1-mesa-dev \
        libgl1-mesa-dev \
        libjack-jackd2-dev \
        libpulse-dev \
        libwayland-dev \
        libx11-dev \
        libxcomposite-dev \
        libxcursor-dev \
        libxext-dev \
        libxfixes-dev \
        libxi-dev \
        libxinerama-dev \
        libxkbcommon-dev \
        libxrandr-dev \
        libxrender-dev \
        libxss-dev \
        make \
        ninja-build \
        perl \
        pkg-config \
        python3 \
        wayland-protocols \
    && rm -rf /var/lib/apt/lists/*
