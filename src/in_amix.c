/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// in_amix.c -- keyboard input for AMIX SVR4
//
// There are two paths here and the difference between them is the difference
// between a game you can play and a game you can demonstrate.
//
// THE RAW PATH, which is the one that gets used on the physical console.
// AMIX's /dev/screen can be put into SIM_RAWKEY mode, after which it reports
// Amiga raw key codes for presses AND releases.  That gives Quake everything a
// keyboard is supposed to give it: a real Ctrl, Shift and Alt -- which are its
// three default action keys -- a real key-up, and any number of keys held at
// once.  Forward and strafe at the same time while firing is this path and
// nothing else.
//
// THE TTY PATH, which is what is left when the raw path is not available: a
// telnet session, or -ttyinput.  A tty reports key PRESSES and never a
// release, so a held key has to be inferred from the terminal's auto-repeat --
// the key counts as down while bytes keep arriving and comes up once they stop
// for `in_keyhold` milliseconds.  That is a guess, and it feels like one:
//
//   too low  -- the gap before auto-repeat starts (typically ~500 ms) reads as
//               a release, and movement stutters at the beginning of every hold
//   too high -- you keep moving after letting go
//
// Ctrl, Shift and Alt do not reach a tty at all, so on this path they simply
// do not exist.  `bind SPACE +attack` is the usual way around that.
//
// The port established the tty details on the real machine (they came from the
// sibling wolf3d-amix port): AMIX's console sends the 8-bit CSI introducer for
// arrow keys rather than the two-byte ESC [ sequence, and SIGTTIN/SIGTTOU have
// to be ignored before touching the tty because opening a screen can move the
// process out of the console's foreground process group.
//
// Mouse input is not implemented on either path.  /dev/screen defines an
// IETYPE_MOUSE event, but the header offers no SIM_ mode that asks for one, so
// whether those events ever arrive is an open question -- tools/rawkeyprobe.c
// prints them if they do.

#include <sys/types.h>
#include <termios.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

#include "quakedef.h"

// Both toolchains compile against AMIX's own headers (the GCC 14 build uses
// -nostdinc and points -isystem at the same sysroot), so <sys/screen.h> is
// there for both.  Note what this guard must NOT be: the GCC 14 build is
// produced by a m68k-linux-gnu compiler, which defines __linux__ even though
// the target is Amiga UNIX, so excluding __linux__ here would silently leave
// the faster of the two builds with no raw keyboard at all.
#ifdef AMIX
#include <sys/screen.h>
#define	HAVE_AMIX_RAWKEY	1
#endif

// AMIX's console sends the 8-bit CSI introducer for arrow and navigation keys
// rather than the two-byte ESC [ sequence.  Established by the wolf3d-amix
// port on the real machine; a terminal on the serial line or over telnet sends
// the two-byte form, so both are accepted.
#define	AMIX_CSI	0x9b
#define	AMIX_CTRL_C	0x03

cvar_t	in_keyhold = {"in_keyhold", "250", true};

static struct termios	saved_tty;
static qboolean		tty_saved = false;
static qboolean		have_tty = false;

static double	key_last_seen[256];
static qboolean	key_is_down[256];

// Set from a signal handler and from the raw path's own Ctrl-C, and acted on
// in Sys_SendKeyEvents.  Quitting from inside a handler would run the whole of
// Host_Shutdown there; this way the VA2000 is put back into passthrough from
// ordinary code, which is the difference between a clean exit and a machine
// left without a picture.
static volatile sig_atomic_t	quit_requested;

#ifdef HAVE_AMIX_RAWKEY
static int		screen_fd = -1;
static unsigned char	raw_qualifiers;
#endif


#ifdef HAVE_AMIX_RAWKEY
/*
================
Amiga raw key code -> Quake key code.

The Amiga's raw codes are positional, so this table is the physical keyboard
and owes nothing to any keymap the console happens to be using.  Letters are
lower case on purpose: Quake wants lowercased ascii, and the tty path already
lowercases everything it sees, so neither path has ever produced a capital.

Where a key has no Quake meaning the entry is 0 and the key is dropped.

Two choices worth naming, because they are choices and not transcription:

  * The numeric keypad reports its own codes, mapped here to the same digits
    as the number row.  Quake binds by key code, so without this the keypad
    would be dead rather than merely duplicated.
  * The right Amiga key is a second Ctrl.  Quake's default fire is Ctrl, and
    on an Amiga keyboard the only Ctrl is in the far bottom-left corner --
    which is exactly where the left hand is not, while walking.
================
*/
static int raw_keymap[0x68] =
{
/* 0x00 */	'`',  '1',  '2',  '3',  '4',  '5',  '6',  '7',
/* 0x08 */	'8',  '9',  '0',  '-',  '=',  '\\', 0,    '0',
/* 0x10 */	'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
/* 0x18 */	'o',  'p',  '[',  ']',  0,    '1',  '2',  '3',
/* 0x20 */	'a',  's',  'd',  'f',  'g',  'h',  'j',  'k',
/* 0x28 */	'l',  ';',  '\'', '#',  0,    '4',  '5',  '6',
/* 0x30 */	'<',  'z',  'x',  'c',  'v',  'b',  'n',  'm',
/* 0x38 */	',',  '.',  '/',  0,    '.',  '7',  '8',  '9',
/* 0x40 */	K_SPACE, K_BACKSPACE, K_TAB, K_ENTER,
/* 0x44 */	K_ENTER, K_ESCAPE, K_DEL, 0,
/* 0x48 */	0, 0, '-', 0,
/* 0x4c */	K_UPARROW, K_DOWNARROW, K_RIGHTARROW, K_LEFTARROW,
/* 0x50 */	K_F1, K_F2, K_F3, K_F4,
/* 0x54 */	K_F5, K_F6, K_F7, K_F8,
/* 0x58 */	K_F9, K_F10, '(', ')',
/* 0x5c */	'/', '*', '+', K_PAUSE,
/* 0x60 */	K_SHIFT, K_SHIFT, 0, K_CTRL,
/* 0x64 */	K_ALT, K_ALT, 0, K_CTRL
};


static int IN_RawKeyMap (unsigned int rawcode)
{
	rawcode &= 0x7f;
	if (rawcode >= sizeof(raw_keymap) / sizeof(raw_keymap[0]))
		return 0;
	return raw_keymap[rawcode];
}


static void IN_PostRawKey (unsigned int rawcode, qboolean down)
{
	int	key;

	key = IN_RawKeyMap (rawcode);
	if (key <= 0 || key > 255)
		return;

	key_is_down[key] = down;
	Key_Event (key, down);
}


/*
================
IN_ReconcileQualifiers

A modifier can be pressed or released while some other screen is in front of
ours, and the transition that happened there is one we never saw.  What we do
get is the qualifier mask the kernel stamps on every event, so any modifier
whose bit disagrees with what we last believed gets its missing event
synthesised here.  The AMIX X server reconciles the same way.

The qualifier bit numbers and the raw codes line up: bit i is the key at raw
code 0x60+i, which is why one loop does all eight.  The `expected` test is
what stops this from duplicating the very event being processed -- 0x60+i is
that key going down and 0xe0+i is it coming up.
================
*/
static void IN_ReconcileQualifiers (struct inputevent *ie)
{
	unsigned char	current;
	unsigned char	changed;
	unsigned int	expected;
	int		i;

	current = (unsigned char)(ie->qualifiers & 0xff);
	changed = raw_qualifiers ^ current;

	for (i = 0; i < 8 && changed; i++, changed >>= 1)
	{
		if (!(changed & 1))
			continue;

		expected = ((current & (1 << i)) ? 0x60 : 0xe0) + i;
		if (ie->code != expected)
			IN_PostRawKey (0x60 + i, (current & (1 << i)) != 0);
	}

	raw_qualifiers = current;
}


/*
================
IN_InitRaw

Returns true when /dev/screen is ours and in rawkey mode.

The screen group is the thing that has to be right.  Closing the descriptor
makes the kernel put the group's previous screen back in front, so without a
group there is no way to hand the console back on the way out -- and a telnet
session has no group at all, which is precisely the case that must fall
through to the tty path rather than fail.
================
*/
static qboolean IN_InitRaw (void)
{
	char	*group_string;
	char	*group_end;
	char	name[SCRNAMESIZE];
	long	group_number;
	int	group;

	if (COM_CheckParm ("-ttyinput"))
		return false;

	group_string = getenv ("SCREEN_GROUP");
	if (group_string && group_string[0])
	{
		group_number = strtol (group_string, &group_end, 10);
		if (*group_end || group_number < 0 || group_number >= 20)
			return false;
		group = (int)group_number;
	}
	else
		group = ioctl (STDIN_FILENO, SIOCGETGROUP);

	if (group < 0 || group >= 20)
		return false;

	screen_fd = open ("/dev/screen", O_RDWR);
	if (screen_fd < 0)
		return false;

	if (fcntl (screen_fd, F_SETFL, O_NDELAY) < 0
		|| ioctl (screen_fd, SIOCSETGROUP, group) < 0
		|| ioctl (screen_fd, SIOCSETINPUTMODE, SIM_RAWKEY) < 0)
	{
		close (screen_fd);
		screen_fd = -1;
		return false;
	}

	memset (name, 0, sizeof(name));
	strncpy (name, "quake-amix raw input", sizeof(name) - 1);
	(void) ioctl (screen_fd, SIOCSETNAME, name);

	if (ioctl (screen_fd, SIOCACTIVATE, 0) < 0)
	{
		close (screen_fd);
		screen_fd = -1;
		return false;
	}

	raw_qualifiers = 0;
	printf ("IN_Init: /dev/screen rawkey, group %d\n", group);
	return true;
}


static void IN_ReadRaw (void)
{
	struct inputevent	events[32];
	struct inputevent	*ie;
	int			n, i;

	for ( ; ; )
	{
		n = read (screen_fd, (char *)events, sizeof(events));
		if (n <= 0)
			break;

		n /= sizeof(events[0]);
		for (i = 0; i < n; i++)
		{
			ie = &events[i];
			if (ie->type != IETYPE_RAWKEY)
				continue;

			IN_ReconcileQualifiers (ie);

			// Ctrl-C is the emergency exit on the tty path, where
			// ISIG is off and it arrives as a byte.  Keep it here
			// too -- raw mode has no tty behind it at all, so
			// without this the only way out of a wedged game is
			// another login.  Qualifier bit 3 is Control and C is
			// raw code 0x33.
			if (!(ie->code & 0x80) && (ie->code & 0x7f) == 0x33
				&& (ie->qualifiers & 0x08))
			{
				quit_requested = 1;
				continue;
			}

			IN_PostRawKey (ie->code, (ie->code & 0x80) ? false : true);
		}
	}
}
#endif	/* HAVE_AMIX_RAWKEY */


/*
================
IN_TranslateKey

Returns the Quake key code for one input byte, or 0 for bytes that carry no
key of their own (the introducers of an escape sequence).
================
*/
static int IN_TranslateKey (unsigned char c)
{
	if (c >= 'A' && c <= 'Z')
		return c + ('a' - 'A');	// Quake wants lowercased ascii
	if (c == '\r' || c == '\n')
		return K_ENTER;
	if (c == 0x08 || c == 0x7f)
		return K_BACKSPACE;
	if (c == '\t')
		return K_TAB;
	if (c == 27)
		return K_ESCAPE;
	if (c < 32)
		return 0;		// other control bytes: not a key
	return c;
}

/*
================
IN_TranslateSequence

Decode a CSI sequence.  Returns the key code, and reports through *used how
many bytes past the introducer were consumed.
================
*/
static int IN_TranslateSequence (unsigned char *seq, int avail, int *used)
{
	*used = 1;
	if (avail < 1)
		return 0;

	switch (seq[0])
	{
	case 'A': return K_UPARROW;
	case 'B': return K_DOWNARROW;
	case 'C': return K_RIGHTARROW;
	case 'D': return K_LEFTARROW;
	}

	// Numeric forms, "<n>~".
	if (avail >= 2 && seq[1] == '~')
	{
		*used = 2;
		switch (seq[0])
		{
		case '1': case '7': return K_HOME;
		case '2': return K_INS;
		case '3': return K_DEL;
		case '4': case '8': return K_END;
		case '5': return K_PGUP;
		case '6': case '9': return K_PGDN;
		}
	}

	return 0;
}

/*
================
IN_TtyKeyDown

Report a press only on the transition, and remember when the key was last
seen so the expiry pass below can decide when it came up.
================
*/
static void IN_TtyKeyDown (int key, double now)
{
	if (key <= 0 || key > 255)
		return;

	if (!key_is_down[key])
	{
		key_is_down[key] = true;
		Key_Event (key, true);
	}
	key_last_seen[key] = now;
}


static void IN_ReadTty (void)
{
	unsigned char	buf[64];
	struct pollfd	pfd;
	double		now, hold;
	int		n, i, key, used;

	now = Sys_FloatTime ();

	// Drain everything the terminal has for us.
	for ( ; ; )
	{
		pfd.fd = STDIN_FILENO;
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll (&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN))
			break;

		n = read (STDIN_FILENO, (char *)buf, sizeof(buf));
		if (n <= 0)
			break;

		for (i = 0; i < n; i++)
		{
			if (buf[i] == AMIX_CSI)
			{
				key = IN_TranslateSequence (buf + i + 1, n - i - 1, &used);
				i += used;
			}
			else if (buf[i] == 27 && i + 1 < n && buf[i+1] == '[')
			{
				// Two-byte ESC [ form, from a serial or telnet terminal.
				key = IN_TranslateSequence (buf + i + 2, n - i - 2, &used);
				i += 1 + used;
			}
			else if (buf[i] == AMIX_CTRL_C)
			{
				// ISIG is off, so this is a byte and not a signal.
				quit_requested = 1;
				key = 0;
			}
			else
			{
				key = IN_TranslateKey (buf[i]);
			}

			IN_TtyKeyDown (key, now);
		}
	}

	// Release anything that has stopped repeating.
	hold = in_keyhold.value / 1000.0;
	if (hold <= 0)
		hold = 0.25;

	for (key = 0; key < 256; key++)
	{
		if (key_is_down[key] && now - key_last_seen[key] > hold)
		{
			key_is_down[key] = false;
			Key_Event (key, false);
		}
	}
}


static void IN_SignalQuit (int sig)
{
	(void) sig;
	quit_requested = 1;
}

/*
================
Sys_SendKeyEvents

Called once a frame from Host_Frame.  id's svgalib port keeps this in
vid_svgalib.c because that is where its keyboard lives; on AMIX the keyboard
is here, on whichever of the two paths came up.
================
*/
void Sys_SendKeyEvents (void)
{
#ifdef HAVE_AMIX_RAWKEY
	if (screen_fd >= 0)
		IN_ReadRaw ();
	else
#endif
	if (have_tty)
		IN_ReadTty ();

	// Ctrl-C, SIGTERM, or a closed session.  Doing this here rather than in
	// the handler means Host_Shutdown runs from ordinary code, and so the
	// VA2000 actually gets put back into passthrough.
	if (quit_requested)
		Sys_Quit ();
}

/*
================
IN_Init
================
*/
void IN_Init (void)
{
	struct termios	raw;

	Cvar_RegisterVariable (&in_keyhold);

	memset ((char *)key_is_down, 0, sizeof(key_is_down));
	memset ((char *)key_last_seen, 0, sizeof(key_last_seen));

	// Opening a screen can move this process out of the console's foreground
	// process group, at which point touching the tty would stop it.  Ignoring
	// these turns that into an error return instead.  From the wolf3d-amix
	// port, where it was found the hard way.  Do it before either path,
	// because either path can be the one that touches the tty.
	signal (SIGTTIN, SIG_IGN);
	signal (SIGTTOU, SIG_IGN);

#ifdef HAVE_AMIX_RAWKEY
	// Note the ordering, and that it differs from the doom-amix port's: id
	// calls IN_Init before VID_Init (see host.c, "mouse comes before video
	// for security reasons"), so this claims the screen before the VA2000 is
	// mapped, where doom-amix claims it after.  The two devices are
	// independent -- /dev/screen selects which Amiga screen is in front, the
	// VA2000 switches its own output in its own registers -- and the one
	// interaction that does exist, the foreground process group, is what the
	// two signals above are for.  If the console ever misbehaves on hardware
	// in a way that smells like ordering, this is the paragraph to doubt.
	if (IN_InitRaw ())
		goto installed;
#endif

	// stdin is not always a terminal: a benchmark run is started with
	// </dev/null so that closing the session cannot take it down.  Leave the
	// tty alone in that case rather than failing.  This is also what keeps a
	// headless run from ever taking the console away from somebody.
	if (!isatty (STDIN_FILENO))
	{
		have_tty = false;
		printf ("IN_Init: no terminal and no screen; keyboard disabled\n");
		goto installed;
	}

	if (tcgetattr (STDIN_FILENO, &saved_tty) != 0)
	{
		have_tty = false;
		printf ("IN_Init: tcgetattr failed; keyboard disabled\n");
		goto installed;
	}
	tty_saved = true;

	raw = saved_tty;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG);
	raw.c_iflag &= ~(IXON | ICRNL);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	tcsetattr (STDIN_FILENO, TCSANOW, &raw);

	have_tty = true;
	printf ("IN_Init: tty fallback, in_keyhold %d ms; no Ctrl, Shift or Alt\n",
			(int)in_keyhold.value);

installed:
	// ISIG is off on the tty path and there is no tty at all on the raw one,
	// so Ctrl-C is not going to arrive as a signal from the keyboard.  Keep a
	// handler anyway: it is how `kill <pid>` from another session becomes a
	// clean exit rather than a black screen.
	//
	// signal() and not sigaction(): AMIX's <sys/signal.h> only declares
	// struct sigaction under _POSIX_SOURCE, which hides other things this
	// port needs.
	signal (SIGINT, IN_SignalQuit);
	signal (SIGTERM, IN_SignalQuit);

	// SIGHUP only if it is not already ignored.  nohup sets SIG_IGN, and
	// replacing that would defeat the point of nohup -- closing the telnet
	// session would then kill a benchmark that was deliberately detached
	// from it.
	if (signal (SIGHUP, SIG_IGN) != SIG_IGN)
		signal (SIGHUP, IN_SignalQuit);
}

/*
================
IN_Shutdown
================
*/
void IN_Shutdown (void)
{
#ifdef HAVE_AMIX_RAWKEY
	// Closing this is what puts the group's previous screen back in front and
	// gives the keyboard back to it.
	if (screen_fd >= 0)
	{
		close (screen_fd);
		screen_fd = -1;
		raw_qualifiers = 0;
	}
#endif

	if (tty_saved)
	{
		tcsetattr (STDIN_FILENO, TCSANOW, &saved_tty);
		tty_saved = false;
	}
	have_tty = false;

	signal (SIGINT, SIG_DFL);
}

void IN_Commands (void)
{
}

void IN_Move (usercmd_t *cmd)
{
}
