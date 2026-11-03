FROM archlinux:base

RUN pacman -Syu --needed --noconfirm \
      alsa-lib \
      base-devel \
      cmake \
      curl \
      ffmpeg \
      ninja \
      pkgconf \
      sqlite \
    && pacman -Scc --noconfirm

WORKDIR /termify
COPY . .

RUN make package \
    && scripts/verify-release-package.sh linux x86_64
