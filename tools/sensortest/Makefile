# Three-sensor doorway test (libgpiod v2).
CC      ?= gcc
CFLAGS  := -std=c17 -Wall -Wextra -Werror -O2 -g $(shell pkg-config --cflags libgpiod)
LDLIBS  := $(shell pkg-config --libs libgpiod)

TARGET  := sensortest

.PHONY: all asan test clean

all: $(TARGET)

$(TARGET): sensortest.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# AddressSanitizer + UBSan build for leak/memory checks.
asan: sensortest.c
	$(CC) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
		-o $(TARGET)-asan $< $(LDLIBS)

# Hardware smoke test on the Pi: line settings + clean ASan exit.
test: asan
	sh ./smoketest.sh

clean:
	rm -f $(TARGET) $(TARGET)-asan
