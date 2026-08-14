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
// in_amix.c -- keyboard input for AMIX SVR4, through the console tty
//
// There is no keyboard device to open here: the console is a tty, so the only
// thing available is the byte stream.  That has one consequence worth stating
// plainly, because it shapes everything below:
//
//   A tty reports key PRESSES.  It never reports a release.
//
// Quake wants both -- holding a movement key is Key_Event(k,true) followed by
// Key_Event(k,false) when the key comes up.  What we do instead is lean on the
// terminal's auto-repeat: a held key keeps delivering bytes, so a key is
// treated as down while bytes keep arriving and released once they stop for
// `in_keyhold` milliseconds.
//
// That value is the whole feel of the controls and cannot be tuned without a
// real keyboard on real hardware, so it is a cvar rather than a constant:
//
//   too low  -- the gap before auto-repeat starts (typically ~500 ms) reads as
//               a release, and movement stutters at the beginning of every hold
//   too high -- you keep moving after letting go
//
// Try `in_keyhold 250` first; if walking stutters, raise it toward the repeat
// delay, and if the player overruns, lower it.
//
// Mouse input is not implemented.  AMIX has a mouse device for X, but reading
// it from a console program is a separate exercise and the first goal here is
// a measurable renderer.

#include <sys/types.h>
#include <termios.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>

#include "quakedef.h"

// AMIX's console sends the 8-bit CSI introducer for arrow and navigation keys
// rather than the two-byte ESC [ sequence.  Established by the wolf3d-amix
// port on the real machine; a terminal on the serial line or over telnet sends
// the two-byte form, so both are accepted.
#define	AMIX_CSI	0x9b

cvar_t	in_keyhold = {"in_keyhold", "250", true};

static struct termios	saved_tty;
static qboolean		tty_saved = false;
static qboolean		have_tty = false;

static double	key_last_seen[256];
static qboolean	key_is_down[256];

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

Decode a CSI sequence.  Returns the key code, and advances *len by the number
of bytes consumed beyond the introducer.
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
IN_KeyDown

Report a press only on the transition, and remember when the key was last
seen so Sys_SendKeyEvents can decide when it came up.
================
*/
static void IN_KeyDown (int key, double now)
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

/*
================
Sys_SendKeyEvents

Called once per frame from the host loop.  id's svgalib port keeps this in
vid_svgalib.c because that is where its keyboard lives; on AMIX the keyboard
is the tty, so it belongs here.
================
*/
void Sys_SendKeyEvents (void)
{
	unsigned char	buf[64];
	struct pollfd	pfd;
	double		now;
	double		hold;
	int		n, i, key, used;

	if (!have_tty)
		return;

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
			else
			{
				key = IN_TranslateKey (buf[i]);
			}

			IN_KeyDown (key, now);
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

	// stdin is not always a terminal: a benchmark run is started with
	// </dev/null so that closing the session cannot take it down.  Leave the
	// tty alone in that case rather than failing.
	if (!isatty (STDIN_FILENO))
	{
		have_tty = false;
		return;
	}

	// Opening the RTG screen can move this process out of the console's
	// foreground process group, at which point touching the tty would stop it.
	// Ignoring these turns that into an error return instead.  From the
	// wolf3d-amix port, where it was found the hard way.
	signal (SIGTTIN, SIG_IGN);
	signal (SIGTTOU, SIG_IGN);

	if (tcgetattr (STDIN_FILENO, &saved_tty) != 0)
	{
		have_tty = false;
		return;
	}
	tty_saved = true;

	raw = saved_tty;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG);
	raw.c_iflag &= ~(IXON | ICRNL);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	tcsetattr (STDIN_FILENO, TCSANOW, &raw);

	have_tty = true;
}

/*
================
IN_Shutdown
================
*/
void IN_Shutdown (void)
{
	if (tty_saved)
	{
		tcsetattr (STDIN_FILENO, TCSANOW, &saved_tty);
		tty_saved = false;
	}
	have_tty = false;
}

void IN_Commands (void)
{
}

void IN_Move (usercmd_t *cmd)
{
}
