CC      ?= cc
CFLAGS  ?= -std=c11 -O3 -Wall -Wextra -Wpedantic
LDFLAGS ?= -lm

UNAME_M := $(shell uname -m)
ifneq (,$(filter aarch64 arm64 armv7l armv8l,$(UNAME_M)))
    CFLAGS += -mtune=native
endif

TARGET  := mini_blender
SRC     := src/mini_blender.c

.PHONY: all clean run stress

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) 0 12 24 renders

stress: $(TARGET)
	./$(TARGET) 0 24 48 renders scenes/sample_env.xml | tee logs/stress_v2.log

clean:
	rm -f $(TARGET)
