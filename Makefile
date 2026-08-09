CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
CFLAGS  += $(shell pkg-config --cflags gtk+-3.0)
LDLIBS   = $(shell pkg-config --libs gtk+-3.0) -lbluetooth

SRCS = src/bt_reader.c src/bt_link.c src/bt_log.c src/bt_pair_linux.c
HDRS = src/bt_link.h src/bt_pair.h src/bt_log.h

bt_reader: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDLIBS)

clean:
	rm -f bt_reader

.PHONY: clean
