CC ?= cc
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -O2 -I.
LDLIBS ?= -pthread

LIB := libkvdb.so
TARGET := libkvdb_test
OBJS := kvdb.o tests/test_kvdb.o

.PHONY: all clean demo test

all: $(LIB) $(TARGET)

$(LIB): kvdb.c kvdb.h
	$(CC) $(CFLAGS) -fPIC -shared -o $@ kvdb.c $(LDLIBS)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDLIBS)

%.o: %.c kvdb.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TARGET)
	tests/run.sh

demo: $(TARGET)
	tests/demo.sh

clean:
	rm -f $(LIB) $(TARGET) $(OBJS)
