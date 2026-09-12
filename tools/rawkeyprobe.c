/*
 * rawkeyprobe.c -- what AMIX's /dev/screen actually reports.
 *
 * src/in_amix.c turns Amiga raw key codes into Quake key codes through a
 * table.  This prints the codes, so that the table can be checked against a
 * real keyboard without running Quake, and so that a key which does nothing
 * in the game can be told apart from a key the kernel never reported.
 *
 *   rawkeyprobe                      safe: does not touch the active screen
 *   rawkeyprobe -capture GROUP [SEC] takes the keyboard for SEC seconds
 *
 * The default run only opens the clone device, asks for rawkey mode, prints
 * the screen groups and exits.  Capture is separate because it activates a
 * screen of its own, which takes the display and the keyboard away from
 * whatever has them -- so it belongs on the physical console and nowhere
 * else.  Closing the descriptor puts the group's previous screen back.
 * Escape ends a capture early.
 *
 * IT ALSO WATCHES FOR MOUSE EVENTS, and that is the open question it exists
 * to answer.  <sys/screen.h> defines IETYPE_MOUSE with IECLASS_MOUSEBUTTON
 * and IECLASS_MOUSEMOTION, but the only input modes it offers are SIM_RAWKEY
 * and SIM_MENU -- there is no SIM_MOUSE to ask with.  Whether mouse events
 * turn up on this descriptor anyway decides whether Quake can have mouselook
 * on the console.  Move the mouse during a capture and read the summary.
 *
 * Build:  make rawkeyprobe          (see the Makefile)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/screen.h>

#define	ESC_RAWCODE	0x45

static int			screen_fd = -1;
static volatile sig_atomic_t	stop_requested;
static long			mouse_events;

static void close_screen ()
{
	if (screen_fd >= 0)
	{
		close (screen_fd);
		screen_fd = -1;
	}
}

static void stop_probe (sig)
int sig;
{
	stop_requested = 1;
}

static void print_groups (fd)
int fd;
{
	struct groupinfo	gi;
	int			i;

	memset ((char *)&gi, 0, sizeof(gi));
	if (ioctl (fd, SIOCGETGROUPINFO, &gi) < 0)
	{
		perror ("SIOCGETGROUPINFO");
		return;
	}

	printf ("screen stamp %ld\n", gi.idstamp);
	for (i = 0; i < 20; i++)
	{
		if (gi.scr[i].id || gi.scr[i].group || gi.scr[i].name[0])
			printf ("  id=%d group=%d flags=0x%x name=\"%.*s\"\n",
					gi.scr[i].id, gi.scr[i].group,
					(unsigned int)gi.scr[i].flags,
					SCRNAMESIZE, gi.scr[i].name);
	}
}

static void report (ie)
struct inputevent *ie;
{
	unsigned int	code;

	code = ie->code & 0x7f;

	if (ie->type == IETYPE_MOUSE)
	{
		mouse_events++;
		printf ("MOUSE   class=%u code=0x%04x qualifiers=0x%08lx%s\n",
				(unsigned int)ie->class, (unsigned int)ie->code,
				ie->qualifiers,
				ie->class == IECLASS_MOUSEMOTION ? "  (motion)" :
				ie->class == IECLASS_MOUSEBUTTON ? "  (button)" : "");
		return;
	}

	if (ie->type != IETYPE_RAWKEY)
	{
		printf ("other   type=%u class=%u code=0x%04x qualifiers=0x%08lx\n",
				(unsigned int)ie->type, (unsigned int)ie->class,
				(unsigned int)ie->code, ie->qualifiers);
		return;
	}

	/* Bit 7 of the code is the release flag, so the key itself is the low
	   seven bits -- that is the number to look up in src/in_amix.c. */
	printf ("rawkey  0x%02x %-4s qualifiers=0x%08lx\n",
			code, (ie->code & 0x80) ? "up" : "down", ie->qualifiers);
}

static int capture (fd, seconds)
int fd;
int seconds;
{
	struct inputevent	events[32];
	struct pollfd		input;
	time_t			end;
	int			n, i;

	end = time ((time_t *)0) + seconds;
	printf ("rawkey capture for %d s; Escape stops it\n", seconds);
	printf ("hold a chord -- arrow + Ctrl + Shift -- and check that every\n");
	printf ("key gives its own down and its own up\n");
	fflush (stdout);

	input.fd = fd;
	input.events = POLLIN;
	input.revents = 0;

	while (!stop_requested && time ((time_t *)0) < end)
	{
		n = poll (&input, 1, 100);
		if (n < 0)
		{
			if (errno == EINTR)
				continue;
			perror ("poll /dev/screen");
			return 1;
		}
		if (n == 0)
			continue;

		n = read (fd, (char *)events, sizeof(events));
		if (n < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			perror ("read /dev/screen");
			return 1;
		}

		n /= sizeof(events[0]);
		for (i = 0; i < n; i++)
		{
			report (&events[i]);
			fflush (stdout);

			if (events[i].type == IETYPE_RAWKEY
				&& !(events[i].code & 0x80)
				&& (events[i].code & 0x7f) == ESC_RAWCODE)
				stop_requested = 1;
		}
	}

	return 0;
}

int main (argc, argv)
int argc;
char **argv;
{
	char	name[SCRNAMESIZE];
	int	do_capture;
	int	group;
	int	seconds;
	int	result;

	setbuf (stdout, (char *)0);

	do_capture = argc >= 2 && !strcmp (argv[1], "-capture");
	group = -1;
	seconds = 15;
	result = 0;

	if (do_capture)
	{
		if (argc < 3)
		{
			fprintf (stderr, "usage: %s [-capture GROUP [SECONDS]]\n", argv[0]);
			return 2;
		}
		group = atoi (argv[2]);
		if (argc >= 4)
			seconds = atoi (argv[3]);
		if (group < 0 || group >= 20 || seconds < 1)
		{
			fprintf (stderr, "invalid screen group or duration\n");
			return 2;
		}
	}

	screen_fd = open ("/dev/screen", O_RDWR);
	if (screen_fd < 0)
	{
		perror ("open /dev/screen");
		return 1;
	}
	atexit (close_screen);

	if (fcntl (screen_fd, F_SETFL, O_NDELAY) < 0)
	{
		perror ("F_SETFL O_NDELAY");
		return 1;
	}
	if (ioctl (screen_fd, SIOCSETINPUTMODE, SIM_RAWKEY) < 0)
	{
		perror ("SIOCSETINPUTMODE SIM_RAWKEY");
		return 1;
	}

	printf ("opened /dev/screen; SIM_RAWKEY accepted\n");
	printf ("this process is in screen group %d\n",
			ioctl (STDIN_FILENO, SIOCGETGROUP));
	print_groups (screen_fd);

	if (!do_capture)
	{
		printf ("PASS (the active screen was not changed)\n");
		return 0;
	}

	if (ioctl (screen_fd, SIOCSETGROUP, group) < 0)
	{
		perror ("SIOCSETGROUP");
		return 1;
	}
	memset (name, 0, sizeof(name));
	strncpy (name, "quake rawkey probe", sizeof(name) - 1);
	(void) ioctl (screen_fd, SIOCSETNAME, name);

	signal (SIGINT, stop_probe);
	signal (SIGTERM, stop_probe);
	signal (SIGHUP, stop_probe);

	if (ioctl (screen_fd, SIOCACTIVATE, 0) < 0)
	{
		perror ("SIOCACTIVATE");
		return 1;
	}

	result = capture (screen_fd, seconds);
	close_screen ();

	printf ("screen closed; input restored to group %d\n", group);
	printf ("mouse events seen: %ld -- %s\n", mouse_events,
			mouse_events ? "SIM_RAWKEY delivers them, so Quake can have a mouse"
						 : "none, so SIM_RAWKEY is keyboard only");
	return result;
}
