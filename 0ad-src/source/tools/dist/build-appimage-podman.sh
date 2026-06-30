#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

podman build -t 0ad-debian-12 -f build/jenkins/dockerfiles/debian-12.Dockerfile
podman build -t 0ad-debian-12-appimage --build-context debian-12=container-image://0ad-debian-12 -f build/jenkins/dockerfiles/debian-12-appimage.Dockerfile

podman run --mount=type=bind,source=.,destination=/mnt --workdir /mnt 0ad-debian-12-appimage source/tools/dist/build-appimage.sh "$@"
