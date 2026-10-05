# Packet Probe engine + web viewer on top of the wirestead-core build image.
#
# WIRESTEAD_CORE_IMAGE: wirestead-container's core image (C++ core under /opt/wirestead).
# WIRESTEAD_PYTHON: pip requirement for wirestead-python, built against that core.
#   Switch to "wirestead==0.10.0" once 0.10.0 is on PyPI.
ARG WIRESTEAD_CORE_IMAGE=ghcr.io/wirestead/wirestead-core:v0.10.0
ARG WIRESTEAD_PYTHON="wirestead @ git+https://github.com/wirestead/wirestead-python@0ac6fa1f77bd4b5e212f17be0b905d4931e67029"

FROM ${WIRESTEAD_CORE_IMAGE} AS build
ARG WIRESTEAD_PYTHON
RUN apt-get update && apt-get install -y --no-install-recommends python3-dev python3-venv \
  && rm -rf /var/lib/apt/lists/*

COPY . /src
RUN cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPACKET_PROBE_BUILD_TESTS=OFF \
  && cmake --build /build --parallel 2

# wirestead-python picks up the image's installed core via CMAKE_PREFIX_PATH.
RUN python3 -m venv /opt/venv \
  && /opt/venv/bin/pip install --no-cache-dir "${WIRESTEAD_PYTHON}" /src/viewer

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends python3 libspdlog1.12 \
  && rm -rf /var/lib/apt/lists/*
COPY --from=build /opt/wirestead/lib/libwirestead.so.0 /usr/local/lib/
COPY --from=build /build/packet-probe /usr/local/bin/packet-probe
COPY --from=build /opt/venv /opt/venv
RUN ldconfig && packet-probe --version && /opt/venv/bin/python -c "import wirestead"

ENV PATH="/opt/venv/bin:${PATH}"
EXPOSE 8080
CMD ["packet-probe-web"]
