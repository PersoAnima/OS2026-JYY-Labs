CC ?= cc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -O2
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
LDLIBS ?= -ldl
endif

TARGET := crepl
SRCS := crepl.c

.PHONY: all clean demo test

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

test: $(TARGET)
	tests/run.sh

demo: $(TARGET)
	tests/demo.sh

clean:
	rm -f $(TARGET)
