CC ?= cc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -O2

TARGET := labyrinth
SRCS := labyrinth.c

.PHONY: all clean demo test

all: $(TARGET)

$(TARGET): $(SRCS) labyrinth.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

test: $(TARGET)
	tests/run.sh

demo: $(TARGET)
	tests/demo.sh

clean:
	rm -f $(TARGET)
