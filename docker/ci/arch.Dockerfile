FROM archlinux:base

RUN pacman -Syu --needed --noconfirm \
      alsa-lib \
      base-devel \
      cmake \
      ffmpeg \
      ninja \
      pkgconf \
      sqlite \
    && pacman -Scc --noconfirm

WORKDIR /termify
COPY . .

RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS=-Werror \
      -DBUILD_TESTING=ON \
    && cmake --build build --parallel 2 \
    && ctest --test-dir build --output-on-failure
