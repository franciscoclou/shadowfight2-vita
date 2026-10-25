# Builds the Linux armhf harness inside shadowfight2-harness (docker/harness).
# Same loader and s3e shims as the Vita build; only the platform differs.
CC      = arm-linux-gnueabihf-gcc
VERBOSE ?= 0
ROOT    = ../..
CFLAGS  = -O1 -g -Wall -Wno-unused-parameter -I$(ROOT)/src -idirafter /usr/include \
          -mfpu=neon -mfloat-abi=hard -DSF_VERBOSE_LOG=$(VERBOSE)
LDFLAGS = -L/usr/lib/arm-linux-gnueabihf -Wl,-rpath-link,/usr/lib/arm-linux-gnueabihf
LIBS    = -lEGL -lGLESv2 -lz -lm -lpthread

SRC = $(wildcard $(ROOT)/src/s3e/*.c) $(ROOT)/src/loader/s3e_loader.c \
      plat_linux.c main_host.c log_host.c

harness: $(SRC) $(wildcard $(ROOT)/src/*/*.h)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LIBS)

test_compress: test_compress.c $(ROOT)/src/s3e/compress.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lz

test_hud: test_hud.c $(ROOT)/src/s3e/hud.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lpthread

test_file: test_file.c $(ROOT)/src/s3e/file.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_memory: test_memory.c $(ROOT)/src/s3e/memory.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lpthread

test: test_compress test_hud test_file test_memory
	qemu-arm ./test_compress
	python3 $(ROOT)/scripts/hud_reference.py $(ROOT)/game_out hud_ref
	SF_HUD_REF=hud_ref qemu-arm ./test_hud
	qemu-arm ./test_file
	qemu-arm ./test_memory

clean:
	rm -rf harness test_compress test_hud test_file test_memory hud_ref
