CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
LDLIBS   = -lbluetooth

bt_reader: src/bt_reader.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f bt_reader

.PHONY: clean
