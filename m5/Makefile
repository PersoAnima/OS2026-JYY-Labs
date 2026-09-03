CC ?= cc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -O2 -I.
LDFLAGS ?=
LDLIBS ?= -pthread

TARGET := test-mymalloc
OBJS := mymalloc.o tests/vmalloc_test.o tests/test_mymalloc.o

.PHONY: all clean demo test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c mymalloc.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TARGET)
	tests/run.sh

demo: $(TARGET)
	tests/demo.sh

clean:
	rm -f $(TARGET) $(OBJS)
