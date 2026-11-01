FROM debian:13-slim

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install --no-install-recommends -y \
      cmake \
      g++ \
      libasound2-dev \
      libavcodec-dev \
      libavformat-dev \
      libavutil-dev \
      libsqlite3-dev \
      libswresample-dev \
      ninja-build \
      pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /termify
COPY . .

RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS=-Werror \
      -DBUILD_TESTING=ON \
    && cmake --build build --parallel 2 \
    && ctest --test-dir build --output-on-failure
