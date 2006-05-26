CC = gcc
CFLAGS = -O2
LDFLAGS =
ICFLAGS = -pthread -pedantic -Wall `xine-config --cflags`
ILDFLAGS = -lX11 -lpthread `xine-config --libs` -L/usr/X11/lib
PREFIX = /usr/local
REMOVE = /bin/rm -f
INSTALL = /usr/bin/install
MKDIR = /bin/mkdir

battery_monitor: battery_monitor.o
	$(CC) $(ICFLAGS) -o battery_monitor battery_monitor.c $(ILDFLAGS) $(LDFLAGS)

battery_monitor.o: battery_monitor.c
	$(CC) $(ICFLAGS) $(CFLAGS) -c battery_monitor.c

install: battery_monitor
	$(MKDIR) -p $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) battery_monitor -m u=rwx,go=rx $(DESTDIR)$(PREFIX)/bin
clean:
	$(REMOVE) *~
	$(REMOVE) battery_monitor
	$(REMOVE) battery_monitor.o
