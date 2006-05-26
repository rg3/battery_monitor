CC ?= gcc
CFLAGS ?= -I/usr/include/nptl -O2
LDFLAGS ?= -L/usr/lib/nptl -L/usr/X11/lib -lpthread -lX11 `xine-config --libs`
FLAGS = -pthread -pedantic -Wall `xine-config --cflags`

battery_monitor: battery_monitor.c
	$(CC) $(FLAGS) $(CFLAGS) -o battery_monitor battery_monitor.c $(LDFLAGS)

clean:
	/bin/rm -f *~
	/bin/rm -f battery_monitor
