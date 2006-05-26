CC = gcc
CFLAGS = -O2
LDFLAGS = -L/usr/X11/lib
ICFLAGS = -pthread -pedantic -Wall `xine-config --cflags`
ILDFLAGS = -lX11 -pthread `xine-config --libs`
PREFIX = /usr/local
REMOVE = /bin/rm -f
INSTALL = /usr/bin/install
MKDIR = /bin/mkdir

battery_monitor: battery_monitor.o
	$(CC) -o battery_monitor battery_monitor.c $(ILDFLAGS) $(LDFLAGS)

battery_monitor.o: battery_monitor.c
	$(CC) $(ICFLAGS) $(CFLAGS) -c battery_monitor.c

install: battery_monitor
	$(MKDIR) -p -m u=rwx,go=rx $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) battery_monitor -m u=rwx,go=rx $(DESTDIR)$(PREFIX)/bin
clean:
	$(REMOVE) *~
	$(REMOVE) battery_monitor
	$(REMOVE) battery_monitor.o
