# Debian 12
FROM debian:bookworm-slim

# 0 A.D. dependencies.
# Unlike gcc/g++ clang only has a single package for both C/C++
# gcc/g++ versions: 11 12(default)
# clang versions: 13 14(default) 15 16 19
# lld for use with clang
# llvm for objdump (spidermonkey)
ARG DEBIAN_FRONTEND=noninteractive
ARG DEBCONF_NOWARNINGS="yes"
RUN apt-get -qqy update \
    && apt-get upgrade -qqy \
    && apt-get install -qqy --no-install-recommends \
        clang \
        clang-14 \
        cmake \
        curl \
        g++ \
        g++-12 \
        gcc \
        gcc-12 \
        libboost-filesystem-dev \
        libboost-system-dev \
        libcurl4-gnutls-dev \
        libenet-dev \
        libfmt-dev \
        libfreetype-dev \
        libgloox-dev \
        libicu-dev \
        libminiupnpc-dev \
        libogg-dev \
        libopenal-dev \
        libpng-dev \
        libsdl2-dev \
        libsodium-dev \
        libvorbis-dev \
        libwxgtk3.2-dev \
        libxml2-dev \
        lld-14 \
        llvm \
        llvm-14 \
        m4 \
        make \
        patch \
        python-is-python3 \
        python3-dev \
        python3-pip \
        subversion \
        xz-utils \
        zlib1g-dev \
    && apt-get clean

# Install rust and Cargo via rustup, available in Debian12 1.63, need 1.76
ENV RUSTUP_HOME=/usr/local/rust
ENV CARGO_HOME=/usr/local/rust
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- --default-toolchain 1.76.0 -y
# Install cbindgen for building SpiderMonkey
RUN /usr/local/rust/bin/cargo install --locked cbindgen@0.29.0

ENV PATH="${RUSTUP_HOME}/bin:${PATH}"
ENV SHELL=/bin/bash
