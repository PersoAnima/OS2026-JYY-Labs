CC ?= cc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -O2

TARGET := fsrecov
SRCS := fsrecov.c
HDRS := fat32.h

.PHONY: all clean demo test

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

test: $(TARGET)
	tests/run.sh

demo: $(TARGET)
	tests/demo.sh

clean:
	rm -f $(TARGET)
