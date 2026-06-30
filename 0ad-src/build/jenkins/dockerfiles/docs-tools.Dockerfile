FROM debian:latest

ARG DEBIAN_FRONTEND=noninteractive
ARG DEBCONF_NOWARNINGS="yes"
RUN apt-get -qqy update && apt-get install -qqy --no-install-recommends \
      ca-certificates \
      cmake \
      doxygen \
      git-lfs \
      graphviz \
      make \
      python3 \
      subversion \
      xsltproc \
 && apt-get clean

RUN git lfs install --system --skip-smudge
