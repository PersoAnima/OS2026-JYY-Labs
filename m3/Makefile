CC ?= cc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -O2

TARGET := sperf
SRCS := sperf.c

.PHONY: all clean demo test

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

test: $(TARGET)
	tests/run.sh

demo: $(TARGET)
	tests/demo.sh

clean:
	rm -f $(TARGET)
