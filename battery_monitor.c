/*
 * battery_monitor.c
 *
 * Copyright (c) 2006 Ricardo Garcia Gonzalez
 */
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <xine.h>

/*
 * CONSTANTS AND PROTOTYPES
 */

#define ARG_NUM			6
#define ARG_PROGNAME		0
#define ARG_LOWBAT		1
#define ARG_STARTSD		2
#define ARG_STOPSD		3
#define ARG_FONT		4
#define ARG_SDCOMMAND		5

const char *arg_soundfile_lowbat;
const char *arg_soundfile_startsd;
const char *arg_soundfile_stopsd;
const char *arg_win_font;
const char *arg_shutdown_command;

const char FILE_INFO[] =	"/proc/acpi/battery/BAT1/info";
const char FILE_STATE[] =	"/proc/acpi/battery/BAT1/state";

#define WIN_XPOS		0
#define WIN_YPOS		0
#define WIN_PADDING		10 /* pixels */

const char MESSAGE_CHARGED[] =	"Battery charged";
const char MESSAGE_LOW[] =	"LOW BATTERY!";

#define CHECK_PERIOD		20 /* seconds */
#define MAX_WARNS		3

#define SHUTDOWN_WAIT		"2" /* minutes */

#define LINE_MAX		80
#define LINE_BUFSIZE		(LINE_MAX + 2) /* '\n' and '\0' */

/* Boolean type. */
typedef enum { false = 0, true = 1 } bool;

/* Get the "design capacity low" field. Returns -1 in case of problems. */
int get_design_capacity_low(void);

/* Get the "remaining capacity" field. Returns -1 in case of problems. */
int get_remaining_capacity(void);

/* Get the "present" field. Used also by get_charging_state(). */
bool get_present(void);

/* Get the "charging state" field. Returns CHST_INVALID in case of problems. */
typedef enum {
	CHST_INVALID,
	CHST_CHARGING,
	CHST_CHARGED,
	CHST_DISCHARGING,
	CHST_NO_BAT,
	CHST_OTHER
} charging_state;
charging_state get_charging_state(void);

/* Draw sign on screen. */
void x11_sign_init(void);				/* init sign system */
void x11_sign_display(const char *sign, bool *ds);	/* display a sign */
void x11_sign_undisplay(bool *ds);			/* undisplay it */

/* Play an alert sound. */
typedef enum {
	ALERT_LOWBAT = 0,
	ALERT_STARTSHUTDOWN = 1,
	ALERT_STOPSHUTDOWN = 2
} alert_type;
void emit_alert(alert_type alert);
void alert_init(void);					/* init alert system */

/* Start shutdown process. */
void start_shutdown(bool *already_active);

/* Stop shutdown process. */
void stop_shutdown(bool *still_active);

/* Sleep the specified amount of seconds. */
void safe_sleep(long seconds);

/* Parse program arguments. Does not return in case of errors. */
void parse_args(int argc, char *argv[]);



/*
 * MAIN PROGRAM
 */

int main(int argc, char *argv[])
{
	charging_state curstate;	/* current charging state */
	charging_state prevstate;	/* previous charging state */

	int remcap;			/* remaining capacity */
	int lowlimit;			/* low capacity limit */

	int numwarns;			/* number of warnings so far */
	bool shutdown_launched;		/* shutdown process running? */
	bool x11_sign_active;		/* X11 sign active? */

	/* initializations */
	parse_args(argc, argv);
	x11_sign_init();
	alert_init();
	curstate = CHST_INVALID;
	prevstate = CHST_INVALID;
	numwarns = 0;
	shutdown_launched = false;
	x11_sign_active = false;

	/* main loop */
	for (;;) {
		/* get chargning state */
		curstate = get_charging_state();

		/* the big switch: decides what to do based on charging state */
		switch (curstate) {

		case CHST_DISCHARGING:
			/* remove previous signs from other states */
			if (CHST_DISCHARGING != prevstate)
				x11_sign_undisplay(&x11_sign_active);

			/* check low limit */
			lowlimit = get_design_capacity_low();
			if (-1 == lowlimit) {
				fprintf(stderr, "Warning: unable to read low "
							"capacity limit\n");
				break;
			}

			/* check remaining capacity */
			remcap = get_remaining_capacity();
			if (-1 == remcap) {
				fprintf(stderr, "Warning: unable to get "
							"remaining capacity\n");
				break;
			}

			/* low battery: display sign, alert and/or shutdown */
			if (remcap < lowlimit) {
				x11_sign_display(MESSAGE_LOW, &x11_sign_active);
				if (++numwarns >= MAX_WARNS && !shutdown_launched)
					start_shutdown(&shutdown_launched);
				else
					emit_alert(ALERT_LOWBAT);
			}

			break;

			/*
			 * CHST_CHARGED, CHST_NO_BAT, CHST_INVALID and
			 * CHST_CHARGING require similar actions.
			 */
		case CHST_CHARGED:
			/* display, reset warn counter and cancel shutdown */
			x11_sign_display(MESSAGE_CHARGED, &x11_sign_active);
			numwarns = 0;
			stop_shutdown(&shutdown_launched);
			break;

		case CHST_CHARGING:
			/* undisplay, reset and cancel */
			x11_sign_undisplay(&x11_sign_active);
			numwarns = 0;
			stop_shutdown(&shutdown_launched);
			break;

		case CHST_NO_BAT:
			/* undisplay, reset, cancel and warning */
			x11_sign_undisplay(&x11_sign_active);
			numwarns = 0;
			stop_shutdown(&shutdown_launched);
			fprintf(stderr, "Warning: battery not present\n");
			break;

		case CHST_INVALID:
			/* undisplay, reset, cancel and another warning */
			x11_sign_undisplay(&x11_sign_active);
			numwarns = 0;
			stop_shutdown(&shutdown_launched);
			fprintf(stderr, "Warning: unable to read charging "
								"state\n");
			break;

		case CHST_OTHER:
			/* What? */
			fprintf(stderr, "Warning: unknown charging state\n");
			break;

		default:
			assert(false);	/* internal error !!! */
			break;

		}

		/* save previous state and sleep */
		prevstate = curstate;
		safe_sleep(CHECK_PERIOD);
	}

	return EXIT_FAILURE;	/* unreachable */
}



/*
 * FUNCTION IMPLEMENTATIONS
 */

/* Auxiliar function. Read an integer field in a file. Returns -1 on errors. */
int get_integer_field(const char filename[], const char giveaway[],
		      const char sscanfpattern[])
{
	char line[LINE_BUFSIZE];
	int fieldval = -1;

	FILE *info = fopen(filename, "r");
	if (NULL == info)
		return -1;

	while (NULL != fgets(line, LINE_BUFSIZE, info)) {
		/* check line start */
		if (line == strstr(line, giveaway)) {
			if (sscanf(line, sscanfpattern, &fieldval) < 1)
				fieldval = -1;
			break;
		}
	}

	fclose(info);
	return fieldval;
}

int get_design_capacity_low(void)
{
	static const char lowcapgiveaway[] = "design capacity low:";
	static const char lowcapvalpattern[] = "%*s%*s%*s%d%*s\n";
	return get_integer_field(FILE_INFO, lowcapgiveaway, lowcapvalpattern);
}

int get_present_rate(void)
{
	static const char currategiveaway[] = "present rate:";
	static const char curratepattern[] = "%*s%*s%d%*s\n";
	return get_integer_field(FILE_STATE, currategiveaway, curratepattern);
}

int get_remaining_capacity(void)
{
	static const char remcapgiveaway[] = "remaining capacity:";
	static const char remcappattern[] = "%*s%*s%d%*s\n";
	return get_integer_field(FILE_STATE, remcapgiveaway, remcappattern);
}

/* Auxiliar function. Read a string field in a file. Returns -1 on errors. */
int get_string_field(const char filename[], const char giveaway[],
		     const char sscanfpattern[], char field[LINE_BUFSIZE])
{
	char line[LINE_BUFSIZE];
	int retstate = -1;

	FILE *info = fopen(filename, "r");
	if (NULL == info)
		return -1;

	while (NULL != fgets(line, LINE_BUFSIZE, info)) {
		/* check line start */
		if (line == strstr(line, giveaway)) {
			if (sscanf(line, sscanfpattern, field) < 1)
				retstate = -1;
			else
				retstate = 0;
			break;
		}
	}

	fclose(info);
	return retstate;
}

bool get_present(void)
{
	static const char psgiveaway[] = "present:";
	static const char pspattern[] = "%*s%s\n";

	char present[LINE_BUFSIZE];

	if (-1 == get_string_field(FILE_STATE, psgiveaway, pspattern, present))
		return false;

	if (strcmp(present, "yes") == 0)
		return true;
	return false;
}

charging_state get_charging_state(void)
{
	static const char csgiveaway[] = "charging state:";
	static const char cspattern[] = "%*s%*s%s\n";

	char state[LINE_BUFSIZE];

	if (! get_present())
		return CHST_NO_BAT;

	if (-1 == get_string_field(FILE_STATE, csgiveaway, cspattern, state))
		return CHST_INVALID;

	if (strcmp(state, "charging") == 0)
		return CHST_CHARGING;
	if (strcmp(state, "charged") == 0)
		return CHST_CHARGED;
	if (strcmp(state, "discharging") == 0)
		return CHST_DISCHARGING;
	return CHST_OTHER;
}

/* Auxiliar function. Create a thread in detached mode. */
int pthread_create_dt(pthread_t *th, void *(*rout)(void *), void *arg)
{
	int retval;
	pthread_attr_t at;

	assert(0 == pthread_attr_init(&at));
	assert(0 == pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED));
	retval = pthread_create(th, &at, rout, arg);
	assert(0 == pthread_attr_destroy(&at));

	return retval;
}

/* Auxiliar structure to pass data to redraw control thread. */
struct drawing_data {
	Display *display;
	Window win;
	GC context;
	int xpos;
	int ypos;
	char *message;
	size_t message_len;
};

/* Auxiliar thread to take care of sign redrawing while X11 thread waits. */
void *x11_redraw_routine(void *pdd)
{
	struct drawing_data *dd = (struct drawing_data *)pdd;
	XEvent ev;

	/* prepare thread */
	assert(0 == pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL));
	assert(0 == pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL));

	/* redrawing loop */
	for (;;) {
		XNextEvent(dd->display, &ev);
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count != 0)
				break;
		case MapNotify:
			XDrawString(dd->display, dd->win, dd->context,
				    dd->xpos, dd->ypos,
				    dd->message, dd->message_len);
			XFlush(dd->display);
			break;
		default:
			break;
		}
	}

	/* unreachable, the thread will be cancelled */
	return NULL;
}

/* Auxiliar semaphore to sync main program and X11 thread. */
sem_t x11_thread_semaphore;

/* Auxiliar thread to take care of setting up X11 sign and wait. */
void *x11_thread_routine(void *msg)
{
	pthread_t redraw_thread;

	/* X11 related data */
	struct drawing_data dd;
	int screen;
	Colormap cmap;
	XColor color;
	unsigned long color_background;
	unsigned long color_foreground;
	XFontStruct *font = NULL;
	XSetWindowAttributes attr;
	int winwidth;
	int winheight;

	/* retrieve message */
	dd.message = (char *) msg;
	dd.message_len = strlen(msg);

	/* open display */
	dd.display = NULL;
	dd.display = XOpenDisplay(NULL);
	if (NULL == dd.display) {
		fprintf(stderr, "Warning: unable to open display\n");
		goto free_and_exit;
	}

	/* get default screen */
	screen = DefaultScreen(dd.display);
	
	/* get colors */
	cmap = DefaultColormap(dd.display, screen);
	assert(0 != XAllocNamedColor(dd.display, cmap, "red", &color, &color));
	color_background = color.pixel;
	assert(0 != XAllocNamedColor(dd.display, cmap, "white", &color, &color));
	color_foreground = color.pixel;

	/* get a font for the text */
	font = XLoadQueryFont(dd.display, arg_win_font);
	if (NULL == font) {
		fprintf(stderr, "Warning: unable to load font %s\n",
			arg_win_font);
		goto free_and_exit;
	}

	/* create window */
	dd.xpos = WIN_PADDING;
	dd.ypos = font->ascent + WIN_PADDING;
	winwidth = XTextWidth(font, dd.message, dd.message_len)
						+ WIN_PADDING + WIN_PADDING;
	winheight = dd.ypos + font->descent + WIN_PADDING;

	attr.background_pixel = color_background;
	attr.override_redirect = True;

	dd.win = XCreateWindow(dd.display, RootWindow(dd.display, screen),
			       WIN_XPOS, WIN_YPOS,
			       winwidth, winheight,
			       0, 	/* border width */
			       CopyFromParent, InputOutput, CopyFromParent,
			       CWOverrideRedirect | CWBackPixel,
			       &attr);

	switch (dd.win) {
	case BadAlloc:
		fprintf(stderr, "Warning: unable to create window\n");
		goto free_and_exit;
		break;
	case BadColor:
	case BadCursor:
	case BadMatch:
	case BadPixmap:
	case BadValue:
	case BadWindow:
		assert(false); /* internal error !!! */
		break;
	default:
		break;
	}

	/* select window events to receive */
	assert(BadWindow != XSelectInput(dd.display, dd.win,
					 StructureNotifyMask | ExposureMask));

	/* map it */
	assert(BadWindow != XMapWindow(dd.display, dd.win));

	/* create graphical context */
	dd.context = XCreateGC(dd.display, dd.win, 0UL, NULL);

	switch (XSetForeground(dd.display, dd.context, color_foreground)) {
	case BadAlloc:
		fprintf(stderr, "Warning: unable to set foreground color\n");
		goto free_and_exit;
		break;
	case BadGC:
	case BadValue:
		assert(false); /* internal error !!! */
		break;
	default:
		break;
	}

	switch (XSetBackground(dd.display, dd.context, color_background)) {
	case BadAlloc:
		fprintf(stderr, "Warning: unable to set background color\n");
		goto free_and_exit;
		break;
	case BadGC:
	case BadValue:
		assert(false); /* internal error !!! */
		break;
	default:
		break;
	}

	switch (XSetFont(dd.display, dd.context, font->fid)) {
	case BadAlloc:
		fprintf(stderr, "Warning: unable to set window font\n");
		goto free_and_exit;
		break;
	case BadGC:
	case BadFont:
		assert(false); /* internal error !!! */
		break;
	default:
		break;
	}

	/* start a redrawing thread */
	assert(0 == pthread_create_dt(&redraw_thread,
				      x11_redraw_routine, (void *)(&dd)));

	/* wait at semaphore, ready to exit */
	assert(0 == sem_wait(&x11_thread_semaphore));

	/* cancel redrawing thread */
	assert(0 == pthread_cancel(redraw_thread));

free_and_exit:
	/* free data and exit */
	if (NULL != dd.display && NULL != font)
		XFreeFont(dd.display, font);
	if (NULL != dd.display)
		XCloseDisplay(dd.display);

	return NULL;
}

void x11_sign_init(void)
{
	/* prepare semaphore */
	assert(0 == sem_init(&x11_thread_semaphore, 0, 0U));
}

void x11_sign_display(const char *sign, bool *sign_up)
{
	pthread_t x11_thread;
	static const char *cur_sign = NULL;

	/* launch the sign routine */
	assert(NULL != sign);
	if (*sign_up && sign == cur_sign)
		return;
	if (*sign_up)
		x11_sign_undisplay(sign_up);
	assert(0 == pthread_create_dt(&x11_thread,
				      x11_thread_routine, (void *)sign));
	*sign_up = true;
	cur_sign = sign;
}

void x11_sign_undisplay(bool *sign_up)
{
	if (*sign_up) {
		/* increment semaphore used in sign routine */
		assert(0 == sem_post(&x11_thread_semaphore));
		*sign_up = false;
	}
}

/* Alert system global variables. */
xine_t *alert_engine = NULL;

/* Initialize alert system */
void alert_init(void)
{
	alert_engine = xine_new();
	if (alert_engine == NULL) {
		fprintf(stderr, "Error: unable to initalize sound system\n");
		exit(EXIT_FAILURE);
	}
	xine_init(alert_engine);
}

/* Auxiliar thread to play sounds according to alert type. */
void *emit_sound_routine(void *al)
{
	xine_audio_port_t *alert_audioport = NULL;
	xine_stream_t *alert_stream = NULL;
	xine_event_queue_t *alert_eventqueue = NULL;
	xine_event_t *alert_event = NULL;

	const char *audiofile = NULL;
	alert_type alert;

	/* recover alert type */
	assert(NULL != al);
	alert = *((alert_type *)al);
	free(al);

	/* select proper sound */
	switch (alert) {
	case ALERT_LOWBAT:
		audiofile = arg_soundfile_lowbat;
		break;
	case ALERT_STARTSHUTDOWN:
		audiofile = arg_soundfile_startsd;
		break;
	case ALERT_STOPSHUTDOWN:
		audiofile = arg_soundfile_stopsd;
		break;
	default:
		assert(false);	/* internal error !!! */
		break;
	}

	/* initialize audio */
	alert_audioport = xine_open_audio_driver(alert_engine, NULL, NULL);
	if (alert_audioport == NULL)
		goto error_exit;

	alert_stream = xine_stream_new(alert_engine, alert_audioport, NULL);
	if (alert_stream == NULL)
		goto error_exit;

	alert_eventqueue = xine_event_new_queue(alert_stream);
	if (alert_eventqueue == NULL)
		goto error_exit;

	/* play it */
	assert(audiofile != NULL);
	if (0 == xine_open(alert_stream, audiofile)) {
		fprintf(stderr, "Warning: unable to open %s\n", audiofile);
		goto clean_exit;
	}
	if (0 == xine_play(alert_stream, 0, 0)) {
		fprintf(stderr, "Warning: unable to play %s\n", audiofile);
		goto clean_exit;
	}
	for (;;) {
		alert_event = xine_event_wait(alert_eventqueue);
		if (alert_event->type == XINE_EVENT_UI_PLAYBACK_FINISHED)
			break;
		xine_event_free(alert_event);
	}
	if (alert_event != NULL) {
		xine_event_free(alert_event);
		alert_event = NULL;
	}
	goto clean_exit;

error_exit:
	fprintf(stderr, "Warning: unable to play alert sound\n");
clean_exit:
	if (alert_eventqueue != NULL)
		xine_event_dispose_queue(alert_eventqueue);
	if (alert_stream != NULL)
		xine_dispose(alert_stream);
	if (alert_audioport != NULL)
		xine_close_audio_driver(alert_engine, alert_audioport);
	return NULL;
}

void emit_alert(alert_type al)
{
	pthread_t sound_thread;
	alert_type *alert;

	/* create thread parameter */
	alert = (alert_type*)malloc(sizeof(alert_type));
	assert(NULL != alert);
	*alert = al;

	/* launch thread */
	assert(0 == pthread_create_dt(&sound_thread,
				      emit_sound_routine, (void *)alert));
}

/* Auxiliar thread to lauch shutdown process. */
void *start_shutdown_routine(void *unused)
{
	static const char launch_args[] = " -h +";
	char *cmdline = NULL;

	/* create command line string */
	cmdline = (char *) malloc((strlen(arg_shutdown_command) +
				   strlen(launch_args) +
				   strlen(SHUTDOWN_WAIT) + 1) * sizeof(char));
	assert(NULL != cmdline);
	strcpy(cmdline, arg_shutdown_command);
	strcat(cmdline, launch_args);
	strcat(cmdline, SHUTDOWN_WAIT);

	/* launch it */
	if (-1 == system(cmdline))
		fprintf(stderr, "Warning: unable to launch shutdown\n");

	/* destroy it and finish */
	free(cmdline);
	return NULL;
}

void start_shutdown(bool *already_active)
{
	pthread_t shutdown_thread;
	if (*already_active)
		return;
	assert(0 == pthread_create_dt(&shutdown_thread,
				      start_shutdown_routine, NULL));
	*already_active = true;
	emit_alert(ALERT_STARTSHUTDOWN);
}

/* Auxiliar thread to stop shutdown process. */
void *stop_shutdown_routine(void *unused)
{
	static const char cancel_args[] = " -c";
	char *cmdline = NULL;

	/* create command line string */
	cmdline = (char *) malloc((strlen(arg_shutdown_command) +
				   strlen(cancel_args) + 1) * sizeof(char));
	assert(NULL != cmdline);
	strcpy(cmdline, arg_shutdown_command);
	strcat(cmdline, cancel_args);

	/* launch it */
	if (-1 == system(cmdline))
		fprintf(stderr, "Warning: unable to stop shutdown\n");

	/* destroy it and finish */
	free(cmdline);
	return NULL;
}

void stop_shutdown(bool *still_active)
{
	pthread_t shutdown_thread;
	if (! *still_active)
		return;
	assert(0 == pthread_create_dt(&shutdown_thread,
				      stop_shutdown_routine, NULL));
	*still_active = false;
	emit_alert(ALERT_STOPSHUTDOWN);
}

void safe_sleep(long seconds)
{
	int fds[2];		/* for pipe() */
	struct timeval sltv;	/* time to wait */
	fd_set readfd;		/* set for select() */

	/* prepare data */
	assert(0 == pipe(fds));
	FD_ZERO(&readfd);
	FD_SET(fds[0], &readfd);
	sltv.tv_sec = seconds;
	sltv.tv_usec = 0L;

	/* we may not sleep much if we catch a signal, but... */
	select(fds[0] + 1, &readfd, NULL, NULL, &sltv);

	/* close file descriptors */
	assert(0 == close(fds[0]));
	assert(0 == close(fds[1]));
}

void parse_args(int argc, char *argv[])
{
	if (argc != ARG_NUM) {
		fprintf(stderr, "Usage: %s %s %s %s %s %s\n\n",
			argv[ARG_PROGNAME],
			"low_battery_wav",
			"start_shutdown_wav",
			"stop_shutdown_wav",
			"window_font",
			"shutdown_command");
		fprintf(stderr, "Please note that the window font must be\n");
		fprintf(stderr, "given in the traditional format, as used\n");
		fprintf(stderr, "by xlsfonts, for example. The shutdown\n");
		fprintf(stderr, "command is usually '/sbin/shutdown', but\n");
		fprintf(stderr, "it is there so you can indicate something\n");
		fprintf(stderr, "like '/usr/bin/sudo /sbin/shutdown'.\n\n");
		exit(EXIT_FAILURE);
	}

	/* set global variables */
	arg_soundfile_lowbat = argv[ARG_LOWBAT];
	arg_soundfile_startsd = argv[ARG_STARTSD];
	arg_soundfile_stopsd = argv[ARG_STOPSD];
	arg_win_font = argv[ARG_FONT];
	arg_shutdown_command = argv[ARG_SDCOMMAND];
}
