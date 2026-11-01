# Build environment for the PS Vita port. Every compile happens inside this image.
FROM vitasdk/vitasdk:latest

# vitaGL and its runtime shader compiler, plus kubridge's user stub (the
# loader needs it to map the game's code executable). The kubridge.skprx
# kernel plugin itself is installed on the Vita, not linked here.
RUN vdpm vitaGL        && \
    vdpm vitashark     && \
    vdpm SceShaccCgExt && \
    vdpm mathneon      && \
    vdpm kubridge      && \
    vdpm zlib          && \
    vdpm libpng        || true

# vitaGL again, from source: the vdpm build lacks HAVE_SHADER_CACHE, so the
# game's GLSL shaders were recompiled with SceShaccCg on every boot and every
# stage load. With it, compiled shaders persist in
# ux0:data/shadowfight2/shader_cache (set in src/platform/vita.c).
# NO_DEBUG skips per-call GL error bookkeeping. That vitaGL needs a newer
# vitashark than vdpm's, so both are built, each pinned to a known commit.
ARG VITASHARK_COMMIT=df24065e65098b2d1ac533760109ad4367573f28
ARG VITAGL_COMMIT=76f43e6fbe1513cee44118da7ff2c16728c4fed2
RUN git init -q /tmp/vitashark && cd /tmp/vitashark && \
    git remote add origin https://github.com/Rinnegatamante/vitaShaRK && \
    git fetch -q --depth 1 origin $VITASHARK_COMMIT && git checkout -q FETCH_HEAD && \
    make -j"$(nproc)" && make install && \
    rm -rf /tmp/vitashark
RUN git init -q /tmp/vitaGL && cd /tmp/vitaGL && \
    git remote add origin https://github.com/Rinnegatamante/vitaGL && \
    git fetch -q --depth 1 origin $VITAGL_COMMIT && git checkout -q FETCH_HEAD && \
    make -j"$(nproc)" HAVE_SHADER_CACHE=1 NO_DEBUG=1 && make install && \
    rm -rf /tmp/vitaGL

# Host-side helpers. The crash-dump symboliser runs in here too, because it
# needs arm-vita-eabi-addr2line and arm-vita-eabi-objdump.
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        bash python3 python3-pyelftools && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
