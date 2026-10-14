# Standalone IR break-beam test (libgpiod v2).
CC      ?= gcc
CFLAGS  := -std=c17 -Wall -Wextra -Werror -O2 -g $(shell pkg-config --cflags libgpiod)
LDLIBS  := $(shell pkg-config --libs libgpiod)

TARGET  := beamtest

.PHONY: all asan clean

all: $(TARGET)

$(TARGET): beamtest.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# AddressSanitizer + UBSan build for leak/memory checks.
asan: beamtest.c
	$(CC) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
		-o $(TARGET)-asan $< $(LDLIBS)

clean:
	rm -f $(TARGET) $(TARGET)-asan
