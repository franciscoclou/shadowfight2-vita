# Host test harness: runs the loader + s3e shims as a Linux armhf program
# under qemu-arm (user mode), with Mesa's software GLES2 behind EGL, so
# boot and rendering can be checked without a Vita. See tests/harness/.
# No binfmt registration needed: qemu-arm is invoked explicitly.
FROM debian:bookworm

RUN dpkg --add-architecture armhf && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        qemu-user gcc-arm-linux-gnueabihf libc6-dev-armhf-cross make python3 python3-pil \
        libc6:armhf libegl1:armhf libgles2:armhf libegl-mesa0:armhf \
        libgl1-mesa-dri:armhf libegl-dev:armhf libgles-dev:armhf libpng-dev:armhf zlib1g-dev:armhf && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
