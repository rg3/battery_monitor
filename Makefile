CC ?= cc
CFLAGS ?= -O2
LDFLAGS ?=

CFLAGS += -pthread -pedantic -Wall `xine-config --cflags`
LDFLAGS += -lX11 -lpthread `xine-config --libs` -L/usr/X11/lib

PREFIX = /usr/local
REMOVE = /bin/rm -f
INSTALL = /usr/bin/install
MKDIR = /bin/mkdir -p

battery_monitor: battery_monitor.o
	$(CC) $(CFLAGS) -o battery_monitor battery_monitor.c $(LDFLAGS)

battery_monitor.o: battery_monitor.c
	$(CC) $(CFLAGS) -c battery_monitor.c

install: battery_monitor
	$(MKDIR) $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) battery_monitor -m u=rwx,go=rx $(DESTDIR)$(PREFIX)/bin
clean:
	$(REMOVE) *~
	$(REMOVE) battery_monitor
	$(REMOVE) battery_monitor.o
