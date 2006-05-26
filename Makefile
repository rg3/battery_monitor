CC ?= gcc
CFLAGS ?= -O2
LDFLAGS ?= -L/usr/X11/lib -lpthread -lX11 `xine-config --libs`
ICFLAGS = -pthread -pedantic -Wall `xine-config --cflags`

battery_monitor: battery_monitor.o
	$(CC) -o battery_monitor battery_monitor.c $(LDFLAGS)

battery_monitor.o: battery_monitor.c
	$(CC) $(ICFLAGS) $(CFLAGS) -c battery_monitor.c

clean:
	/bin/rm -f *~
	/bin/rm -f battery_monitor
	/bin/rm -f battery_monitor.o
