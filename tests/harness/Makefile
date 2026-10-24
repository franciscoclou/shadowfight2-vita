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

clean:
	rm -f harness
