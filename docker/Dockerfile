# Build environment for the PS Vita port.
# The host vitasdk cannot run here (its binaries require glibc 2.38, this
# machine has 2.35), so every compile happens inside this image.
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

# Host-side helpers. The crash-dump symboliser runs in here too, because it
# needs arm-vita-eabi-addr2line and arm-vita-eabi-objdump.
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        bash python3 python3-pyelftools && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
