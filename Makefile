CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
CFLAGS  += $(shell pkg-config --cflags gtk+-3.0)
LDLIBS   = $(shell pkg-config --libs gtk+-3.0) -lbluetooth

INCLUDES = -Ibt -Iui -Iclipboard

SRCS = main.c ui/config.c ui/ui_gtk.c \
       bt/bt_link.c bt/bt_log.c bt/bt_pair_linux.c \
       clipboard/clipboard_linux.c
HDRS = ui/ui.h ui/config.h \
       bt/bt.h bt/bt_link.h bt/bt_pair.h bt/bt_log.h \
       clipboard/clipboard.h

bt_reader: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SRCS) $(LDLIBS)

clean:
	rm -f bt_reader

.PHONY: clean
