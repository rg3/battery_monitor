CC = gcc
CFLAGS = -O2
LDFLAGS =
ICFLAGS = -pthread -pedantic -Wall `xine-config --cflags`
ILDFLAGS = -lX11 -lpthread `xine-config --libs` -L/usr/X11/lib
PREFIX = /usr/local
REMOVE = /bin/rm -f
INSTALL = /usr/bin/install
MKDIR = /bin/mkdir

battery-monitor: battery-monitor.o
	$(CC) $(ICFLAGS) -o battery-monitor battery-monitor.c $(ILDFLAGS) $(LDFLAGS)

battery-monitor.o: battery-monitor.c
	$(CC) $(ICFLAGS) $(CFLAGS) -c battery-monitor.c

install: battery-monitor
	$(MKDIR) -p $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) battery-monitor -m u=rwx,go=rx $(DESTDIR)$(PREFIX)/bin
clean:
	$(REMOVE) *~
	$(REMOVE) battery-monitor
	$(REMOVE) battery-monitor.o
