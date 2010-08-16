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
#include <errno.h>
#include <X11/Xlib.h>
#include <xine.h>

/*
 * CONSTANTS AND PROTOTYPES
 */

#define ARG_NUM_MIN		6
#define ARG_NUM_MAX		7
#define ARG_PROGNAME		0
#define ARG_LOWBAT		1
#define ARG_STARTSD		2
#define ARG_STOPSD		3
#define ARG_FONT		4
#define ARG_SDCOMMAND		5
#define ARG_CHECK_PERIOD	6

const char *arg_soundfile_lowbat;
const char *arg_soundfile_startsd;
const char *arg_soundfile_stopsd;
const char *arg_win_font;
const char *arg_shutdown_command;
int arg_check_period;

const char FILE_INFO[] =	"/proc/acpi/battery/BAT1/info";
const char FILE_STATE[] =	"/proc/acpi/battery/BAT1/state";

#define WIN_XPOS		0
#define WIN_YPOS		0
#define WIN_PADDING		10 /* pixels */

#define	MSG_BATTERY_CHARGED	0
#define MSG_LOW_BATTERY		1
#define MSG_LOWCAP_WARNING	2
#define MSG_REMCAP_WARNING	3
#define MSG_NOTDET_WARNING	4
#define MSG_CHST_READ_WARNING	5
#define	MSG_CHST_UNK_WARNING	6
#define MSG_REMOVE_SIGN		10
const char *x11_signs[] = {
	"Battery charged",
	"LOW BATTERY!",
	"Warning: unable to read low capacity limit",
	"Warning: unable to read remaining capacity",
	"Warning: battery not detected",
	"Warning: unable to read charging state",
	"Warning: unknown charging state",
	NULL
};

#define TEMP_SIGN_TIME		5 /* seconds */
#define CHECK_PERIOD_MIN	1 /* seconds */
#define CHECK_PERIOD_MAX	(24 * 3600) /* seconds */
#define CHECK_PERIOD_DEFAULT	20 /* seconds */
#define SAFETY_TIME		60 /* seconds */

const char SHUTDOWN_WAIT[] =	"2"; /* minutes */

#define LINE_MAX		80
#define LINE_BUFSIZE		(LINE_MAX + 2) /* '\n' and '\0' */
#define READ_FD			0
#define WRITE_FD		1

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
void x11_sign_display(char sign, bool *ds);		/* display a sign */
void x11_sign_display_temp(char sign, bool *ds);	/* temporal display */
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

	int warnnum;			/* number of warnings so far */
	bool shutdown_launched;		/* shutdown process running? */
	bool x11_sign_active;		/* X11 sign active? */

	/* initializations */
	parse_args(argc, argv);
	x11_sign_init();
	alert_init();
	curstate = CHST_INVALID;
	prevstate = CHST_INVALID;
	warnnum = 0;
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
				fprintf(stderr, "%s\n", x11_signs[MSG_LOWCAP_WARNING]);
				x11_sign_display_temp(MSG_LOWCAP_WARNING, &x11_sign_active);
				break;
			}

			/* check remaining capacity */
			remcap = get_remaining_capacity();
			if (-1 == remcap) {
				fprintf(stderr, "%s\n", x11_signs[MSG_REMCAP_WARNING]);
				x11_sign_display_temp(MSG_REMCAP_WARNING, &x11_sign_active);
				break;
			}

			/* low battery: display sign, alert and/or shutdown */
			if (remcap < lowlimit) {
				x11_sign_display(MSG_LOW_BATTERY, &x11_sign_active);
				if (warnnum * arg_check_period >= SAFETY_TIME && !shutdown_launched)
					start_shutdown(&shutdown_launched);
				else {
					warnnum++;
					emit_alert(ALERT_LOWBAT);
				}
			}

			break;

			/*
			 * CHST_CHARGED, CHST_NO_BAT, CHST_INVALID and
			 * CHST_CHARGING require similar actions.
			 */
		case CHST_CHARGED:
			/* display, reset warn counter and cancel shutdown */
			x11_sign_display(MSG_BATTERY_CHARGED, &x11_sign_active);
			warnnum = 0;
			stop_shutdown(&shutdown_launched);
			break;

		case CHST_CHARGING:
			/* undisplay, reset and cancel */
			x11_sign_undisplay(&x11_sign_active);
			warnnum = 0;
			stop_shutdown(&shutdown_launched);
			break;

		case CHST_NO_BAT:
			/* undisplay, reset, cancel and warning */
			x11_sign_undisplay(&x11_sign_active);
			warnnum = 0;
			stop_shutdown(&shutdown_launched);
			fprintf(stderr, "%s\n", x11_signs[MSG_NOTDET_WARNING]);
			break;

		case CHST_INVALID:
			/* undisplay, reset, cancel and another warning */
			x11_sign_undisplay(&x11_sign_active);
			warnnum = 0;
			stop_shutdown(&shutdown_launched);
			fprintf(stderr, "%s\n", x11_signs[MSG_CHST_READ_WARNING]);
			x11_sign_display_temp(MSG_CHST_READ_WARNING, &x11_sign_active);
			break;

		case CHST_OTHER:
			/* What? */
			fprintf(stderr, "%s\n", x11_signs[MSG_CHST_UNK_WARNING]);
			x11_sign_display_temp(MSG_CHST_UNK_WARNING, &x11_sign_active);
			break;

		default:
			assert(false);	/* internal error !!! */
			break;

		}

		/* save previous state and sleep */
		prevstate = curstate;
		safe_sleep(arg_check_period);
	}

	return EXIT_FAILURE;	/* unreachable */
}



/*
 * FUNCTION IMPLEMENTATIONS
 */

/* Auxiliar function. Read an integer field in a file. Returns -1 on errors. */
int get_integer_field(const char filename[], const char giveaway[], const char sscanfpattern[])
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
int get_string_field(const char filename[], const char giveaway[], const char sscanfpattern[], char field[LINE_BUFSIZE])
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

/* drawing data needed for any thread that handles signs */
struct drawing_data {
	Display *display;
	Window win;
	GC context;
	int xpos;
	int ypos;
	XFontStruct *font;
	const char *cur_msg;
	size_t cur_msg_len;
};

/* global drawing data */
struct drawing_data x11_dd;

/* pipe to communicate with sign control routine */
int x11_pipe[2];

/* auxiliar function to send a command to the x11 sign routine */
void x11_send_command(char command)
{
	int retval;
	for (;;) {
		retval = write(x11_pipe[WRITE_FD], &command, sizeof(char));
		if (retval == 0)
			continue;
		if (retval == -1 && errno == EINTR)
			continue;
		assert(retval != -1);
		break;
	}
}

/* auxiliar function to prepare a window for different signs */
void x11_prepare_sign(char command)
{
	unsigned width;
	unsigned height;

	x11_dd.cur_msg = x11_signs[(int)command];
	x11_dd.cur_msg_len = strlen(x11_dd.cur_msg);
	XUnmapWindow(x11_dd.display, x11_dd.win);

	width = XTextWidth(x11_dd.font, x11_dd.cur_msg, x11_dd.cur_msg_len) + WIN_PADDING + WIN_PADDING;
	height = x11_dd.ypos + x11_dd.font->descent + WIN_PADDING;

	XResizeWindow(x11_dd.display, x11_dd.win, width, height);
	XMapWindow(x11_dd.display, x11_dd.win);
	XDrawString(x11_dd.display, x11_dd.win, x11_dd.context, x11_dd.xpos, x11_dd.ypos, x11_dd.cur_msg, x11_dd.cur_msg_len);
}

/* sign control routine, receives commands and responds to events */
void *x11_sign_control_routine(void *unused)
{
	int connection;
	int maxfd;
	fd_set readfds;
	int retval;
	char command;
	XEvent ev;

	connection = ConnectionNumber(x11_dd.display);
	maxfd = (connection > x11_pipe[READ_FD])? connection : x11_pipe[READ_FD];

	for (;;) {
		/* prepare select */
		FD_ZERO(&readfds);
		FD_SET(connection, &readfds);
		FD_SET(x11_pipe[READ_FD], &readfds);

		/* do select */
		while (-1 == (retval = select(maxfd + 1, &readfds, NULL, NULL, NULL)) && errno == EINTR)
			;
		assert(retval != -1);

		/* check commands */
		if (FD_ISSET(x11_pipe[READ_FD], &readfds)) {
			assert(0 != read(x11_pipe[READ_FD], &command, sizeof(char)));

			switch (command) {
			case MSG_BATTERY_CHARGED:
			case MSG_LOW_BATTERY:
			case MSG_LOWCAP_WARNING:
			case MSG_REMCAP_WARNING:
			case MSG_NOTDET_WARNING:
			case MSG_CHST_READ_WARNING:
			case MSG_CHST_UNK_WARNING:
				x11_prepare_sign(command);
				XFlush(x11_dd.display);
				break;
			case MSG_REMOVE_SIGN:
				XUnmapWindow(x11_dd.display, x11_dd.win);
				XFlush(x11_dd.display);
				break;
			default:
				assert(false);
				break;
			}
		}

		/* check X11 events */
		if (FD_ISSET(connection, &readfds)) {
			if (XPending(x11_dd.display) == 0) {
				fprintf(stderr, "Warning: activity in X11 connection but no events\n");
				continue;
			}

			XNextEvent(x11_dd.display, &ev);
			switch (ev.type) {
			case Expose:
				if (ev.xexpose.count != 0)
					break;
			case VisibilityNotify:
			case MapNotify:
				XDrawString(x11_dd.display, x11_dd.win, x11_dd.context, x11_dd.xpos, x11_dd.ypos, x11_dd.cur_msg, x11_dd.cur_msg_len);
				XFlush(x11_dd.display);
				break;
			default:
				break;
			}
		}
	}

	return NULL; /* unreachable */
}

void x11_sign_init(void)
{
	/* prepare drawing data */
	pthread_t control_thread;
	int screen;
	Colormap cmap;
	XColor color;
	unsigned long color_background;
	unsigned long color_foreground;
	XSetWindowAttributes attr;

	/* prepare X11 for multithreading */
	assert(XInitThreads());

	/* prepare sign control pipe */
	assert(0 == pipe(x11_pipe));

	/* open display */
	x11_dd.display = NULL;
	x11_dd.display = XOpenDisplay(NULL);
	assert (NULL != x11_dd.display);

	/* get default screen */
	screen = DefaultScreen(x11_dd.display);

	/* get colors */
	cmap = DefaultColormap(x11_dd.display, screen);
	assert(0 != XAllocNamedColor(x11_dd.display, cmap, "red", &color, &color));
	color_background = color.pixel;
	assert(0 != XAllocNamedColor(x11_dd.display, cmap, "white", &color, &color));
	color_foreground = color.pixel;

	/* get a font for the text */
	x11_dd.font = XLoadQueryFont(x11_dd.display, arg_win_font);
	assert (NULL != x11_dd.font);

	/* create window */
	x11_dd.xpos = WIN_PADDING;
	x11_dd.ypos = x11_dd.font->ascent + WIN_PADDING;
	attr.background_pixel = color_background;
	attr.override_redirect = True;

	x11_dd.win = XCreateWindow(x11_dd.display, RootWindow(x11_dd.display, screen), WIN_XPOS, WIN_YPOS, WIN_PADDING, WIN_PADDING, 0, CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel, &attr);

	switch (x11_dd.win) {
	case BadAlloc:
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
	assert(BadWindow != XSelectInput(x11_dd.display, x11_dd.win, StructureNotifyMask | ExposureMask | VisibilityChangeMask));

	/* create graphical context */
	x11_dd.context = XCreateGC(x11_dd.display, x11_dd.win, 0UL, NULL);

	switch (XSetForeground(x11_dd.display, x11_dd.context, color_foreground)) {
	case BadAlloc:
	case BadGC:
	case BadValue:
		assert(false); /* internal error !!! */
		break;
	default:
		break;
	}

	switch (XSetBackground(x11_dd.display, x11_dd.context, color_background)) {
	case BadAlloc:
	case BadGC:
	case BadValue:
		assert(false); /* internal error !!! */
		break;
	default:
		break;
	}

	switch (XSetFont(x11_dd.display, x11_dd.context, x11_dd.font->fid)) {
	case BadAlloc:
	case BadGC:
	case BadFont:
		assert(false); /* internal error !!! */
		break;
	default:
		break;
	}

	x11_dd.cur_msg = "";
	pthread_create_dt(&control_thread, x11_sign_control_routine, NULL);
}

void x11_sign_display(char sign, bool *sign_up)
{
	static char cur_sign = '\0';

	if (*sign_up && sign == cur_sign)
		return;

	if (*sign_up)
		x11_sign_undisplay(sign_up);

	x11_send_command(sign);
	*sign_up = true;
	cur_sign = sign;
}

/* Auxiliar struct to store arguments for temporal display sign thread */
struct temp_sign_args {
	char sign;
	bool *sign_active;
};

/* Temporal display sign thread routine */
void *x11_temp_sign_control(void *param)
{
	struct temp_sign_args args;

	/* recover args */
	assert(NULL != param);
	args = *((struct temp_sign_args *)param);

	/* free space */
	free(param);

	/* perform job */
	x11_sign_display(args.sign, args.sign_active);
	safe_sleep(TEMP_SIGN_TIME);
	x11_sign_undisplay(args.sign_active);
	return NULL;
}

void x11_sign_display_temp(char sign, bool *ds)
{
	struct temp_sign_args *args;
	pthread_t th;

	/* allocate and store thread arguments */
	args = (struct temp_sign_args *) malloc(sizeof(struct temp_sign_args));
	assert(NULL != args);
	args->sign = sign;
	args->sign_active = ds;

	/* create thread */
	assert(0 == pthread_create_dt(&th, x11_temp_sign_control, (void *)args));
}

void x11_sign_undisplay(bool *sign_up)
{
	if (*sign_up) {
		x11_send_command(MSG_REMOVE_SIGN);
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

	/* recover alert type and free it */
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
	assert(0 == pthread_create_dt(&sound_thread, emit_sound_routine, (void *)alert));
}

/* Auxiliar thread to lauch shutdown process. */
void *start_shutdown_routine(void *unused)
{
	static const char launch_args[] = " -h +";
	char *cmdline = NULL;

	/* create command line string */
	cmdline = (char *) malloc((strlen(arg_shutdown_command) + strlen(launch_args) + strlen(SHUTDOWN_WAIT) + 1) * sizeof(char));
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
	assert(0 == pthread_create_dt(&shutdown_thread, start_shutdown_routine, NULL));
	*already_active = true;
	emit_alert(ALERT_STARTSHUTDOWN);
}

/* Auxiliar thread to stop shutdown process. */
void *stop_shutdown_routine(void *unused)
{
	static const char cancel_args[] = " -c";
	char *cmdline = NULL;

	/* create command line string */
	cmdline = (char *) malloc((strlen(arg_shutdown_command) + strlen(cancel_args) + 1) * sizeof(char));
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
	assert(0 == pthread_create_dt(&shutdown_thread, stop_shutdown_routine, NULL));
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
	char *begin;
	char *end;
	size_t length;
	long aux;

	if (argc < ARG_NUM_MIN || argc > ARG_NUM_MAX) {
		fprintf(stderr, "Usage: %s %s %s %s %s %s [%s]\n\n",
			argv[ARG_PROGNAME],
			"low_battery_wav",
			"start_shutdown_wav",
			"stop_shutdown_wav",
			"window_font",
			"shutdown_command",
			"check_period");
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

	/* parse check period time */
	if (argc == ARG_NUM_MAX) {
		begin = argv[ARG_CHECK_PERIOD];
		length = strlen(begin);
		aux = strtol(argv[ARG_CHECK_PERIOD], &end, 0);

		if (end != (begin + length) || aux < CHECK_PERIOD_MIN || aux > CHECK_PERIOD_MAX) {
			fprintf(stderr, "Error parsing check period time\n");
			exit(EXIT_FAILURE);
		}

		arg_check_period = (int) aux;
	}
	else
		arg_check_period = CHECK_PERIOD_DEFAULT;
}
