 FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libgtk-3-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B /build -G Ninja -DBUILD_TESTING=ON \
 && cmake --build /build

# Binaries are at /build/steamdeckcontrollerd and /build/steamdeckcontroller
