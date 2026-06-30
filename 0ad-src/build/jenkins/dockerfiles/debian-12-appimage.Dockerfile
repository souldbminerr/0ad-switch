FROM debian-12

# minisign only needed for signing in pipeline
RUN apt-get -qqy update \
    && apt-get upgrade -qqy \
    && apt-get install -qqy --no-install-recommends \
        cimg-dev \
        desktop-file-utils \
        file \
        git \
        libgcrypt20-dev \
        libgpgme-dev \
        libjpeg-dev \
        minisign \
        patchelf \
        squashfs-tools \
        wget \
    && apt-get clean

RUN \
  git clone --depth 1 --branch 1-alpha-20250213-2 https://github.com/linuxdeploy/linuxdeploy --recurse-submodules && \
    cd linuxdeploy && cp src/core/copyright/copyright.h src/core && \
    cmake -B build -S . \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DBUILD_TESTING=OFF \
      -DUSE_CCACHE=OFF \
    && cmake --build build --target install && \
    cd .. && \
    rm -rf linuxdeploy

RUN \
    git clone --depth 1 --branch continuous https://github.com/AppImage/appimagetool.git && \
    cd appimagetool && \
    cmake -B build -S . \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build build --target install && \
    cd .. && \
    rm -rf appimagetool
