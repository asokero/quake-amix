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
// vid_va2000.c -- MNT VA2000 RTG framebuffer driver for AMIX SVR4
//
// Structure follows id's own vid_svgalib.c: one high-hunk block holding the
// z-buffer, the surface cache and the render buffer, with vid.direct pointing
// at real screen memory.
//
// The hardware sequence is not guesswork -- it is the one the wolf3d-amix port
// established on the real machine, kept in docs/reference/wolf3d_id_vl_amix.c.ref.
// Note in particular that mode setting and the palette are done by writing the
// board's registers directly.  The va2000-amix driver documents an ioctl
// interface (SVGAIOCSetScreenMode / SVGAIOCSetRGB); the code that actually
// works on hardware does not use it.
//
// SHARING THE BOARD WITH AN X SERVER (-shared).
//
// Started from an X session on this same card, the ordinary path above is
// wrong three times over: it replaces X's mode with its own, it draws over
// X's pixels -- which are at the bottom of video memory, where this draws too
// -- and on exit it hands the display to the Amiga rather than back to X.
// Nothing tells xrtg any of it happened, so nothing redraws.
//
// The obvious fix, save the registers on entry and write them back on exit,
// does not work, and the reason is a measurement the saku26 port made on this
// board: the mode registers are effectively WRITE-ONLY, reading back 0x005a
// whatever was put in them.  The PAN registers do read back.  So the mode
// cannot be restored and must therefore not be disturbed, while the pan can
// be, and that is the whole design:
//
//   - no mode register is written, so X's mode stays up and Quake runs inside
//     whatever resolution X is displaying;
//   - the drawing area is 16 MB up the framebuffer, past anything a screen
//     mode reaches, so X's pixels are never touched;
//   - the pan is saved on entry and written back on exit, so the display
//     returns to exactly where it was pointing -- and since X's pixels are
//     still there, the desktop reappears without a redraw.
//
// Restoring the pan that was actually found, rather than a pan reasoned out
// from who ought to own the board, is deliberate.  saku26 reasoned first,
// restored 0 on the grounds that an X screen sits at the bottom of memory,
// and blanked the display of every machine that had no X server: passthrough
// is not offset 0, it is pan hi = 0xf8.
//
// The console path is unchanged by any of this.  It is selected by the flag
// and nothing else, and VID_Update gains one test outside its loop.

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quakedef.h"
#include "d_local.h"

// AMIX's <sys/mman.h> declares mmap() and nothing else -- no munmap.  Declared
// K&R style, as the header declares mmap, so there is no prototype to disagree
// with the library symbol.
extern int munmap();

viddef_t	vid;				// global video state

#define	BASEWIDTH	320
#define	BASEHEIGHT	200

// The display fetches 480 physical lines and the mode scales 2x, so it reads
// 240 framebuffer rows -- 40 more than the game draws.  Clear all of them or
// the bottom of the screen shows whatever was in board RAM.
#define	FB_ROWS_FETCHED	240

#define	VA2000_DEVICE		"/dev/va2000"
#define	VA2000_FB_OFFSET	0x00010000UL
// The register window is the first 64 KB of the aperture; the framebuffer
// starts after it.  Two mappings rather than one, because in shared mode the
// drawing area is 16 MB up the framebuffer and a single 4 MB map cannot see
// both ends.  Nothing per-frame changes: the pointer arithmetic below is the
// same either way.
#define	VA2000_REGWIN		0x00010000UL

// Where a shared-mode buffer lives, measured from the start of the
// framebuffer.  16 MB up is past anything a screen mode reaches, which is the
// whole point: the X server's pixels sit at the bottom and are never touched,
// so when the display is pointed back at them they are still there.
#define	VA2000_SHARED_BASE	0x01000000UL

// What X is running, when nothing says otherwise.  This is xrtg's own default.
#define	VA2000_SHARED_W		800
#define	VA2000_SHARED_H		600

// Palette CLUT registers: one 16-bit register per component per index.
#define	VA2000_PAL_R		0x200
#define	VA2000_PAL_G		0x400
#define	VA2000_PAL_B		0x600

#define	VA_WREG(off,val) \
	(*((volatile unsigned short *)(va_regs + (off))) = (unsigned short)(val))
#define	VA_RREG(off) \
	(*((volatile unsigned short *)(va_regs + (off))))

static int	va_fd = -1;
static char	*va_regs = (char *)-1;		// register window, (char *)-1 when unmapped
static char	*va_fbwin = (char *)-1;		// framebuffer window
static unsigned long	va_fbwin_len;
static byte	*va_fb = NULL;			// where this program draws

// ---- shared mode ----------------------------------------------------------
// Set by -shared.  The board is then already driving an X server and this
// program is a guest on it: it writes no mode register, draws into its own
// area 16 MB up, and puts the display back where it found it on the way out.
static qboolean		va_shared = false;
static unsigned short	va_saved_pan_hi, va_saved_pan_lo;
static int		va_screen_w = VA2000_SHARED_W;	// what X is running...
static int		va_screen_h = VA2000_SHARED_H;	// ...which cannot be read back
static int		va_ox, va_oy;			// 320x200 centred in that

unsigned short	d_8to16table[256];
unsigned	d_8to24table[256];

static int	VID_highhunkmark;
static byte	*vid_surfcache;
static byte	vid_current_palette[768];

/*
================
VA2000_SetMode

320x200 8bpp, scale mode 5 (2x on both axes) inside an 800x600 40 MHz frame,
so the board outputs 640x400.  Register values from the wolf3d-amix port,
which took them from va2000_restest.c's verified "320x200 8bpp 2x" mode.
================
*/
static void VA2000_SetMode (void)
{
	VA_WREG(0x16, 0x0a);	// refresh timing: required in 8bpp or sync flickers
	VA_WREG(0x70, 840);	// H sync start
	VA_WREG(0x72, 968);	// H sync end
	VA_WREG(0x74, 1056);	// H max
	VA_WREG(0x76, 601);	// V sync start
	VA_WREG(0x78, 605);	// V sync end
	VA_WREG(0x7a, 628);	// V max
	VA_WREG(0x7c, 1);	// pixel clock: CLK_40MHZ
	VA_WREG(0x0e, 0);	// colour mode: CM_8BIT
	VA_WREG(0x58, BASEWIDTH / 2);	// pitch, in 16-bit words
	VA_WREG(0x5c, 9);	// pitch shift
	VA_WREG(0x06, BASEWIDTH * 2);	// display width  (2x scale)
	VA_WREG(0x08, 480);	// display height (200*2=400, minimum frame is 480)
	VA_WREG(0x04, 5);	// scale mode: 2x both axes
	VA_WREG(0x14, 0x1e0);	// safe x2
	VA_WREG(0x18, 0x17);	// RAM fetch
	VA_WREG(0x1a, 0x1e0);	// fetch preroll
	VA_WREG(0x38, 0);	// pan hi: RTG framebuffer
	VA_WREG(0x3a, 0);	// pan lo
	VA_WREG(0x4e, 0);	// capture: RTG mode on

	// The modeline is written twice on purpose; a single pass leaves display
	// glitches.  Also from va2000_restest.c.
	VA_WREG(0x70, 840);
	VA_WREG(0x72, 968);
	VA_WREG(0x74, 1056);
	VA_WREG(0x76, 601);
	VA_WREG(0x78, 605);
	VA_WREG(0x7a, 628);
}

/*
================
VA2000_RestorePassthrough

Hand the display back to the Amiga side.  Skipping this leaves the machine
with no usable picture, which on a headless test rig means a power cycle.
================
*/
static void VA2000_RestorePassthrough (void)
{
	VA_WREG(0x38, 0xf8);	// pan hi: passthrough
	VA_WREG(0x3a, 0x00);
	VA_WREG(0x70, 840);
	VA_WREG(0x72, 968);
	VA_WREG(0x74, 1056);
	VA_WREG(0x76, 601);
	VA_WREG(0x78, 605);
	VA_WREG(0x7a, 628);
	VA_WREG(0x7c, 1);	// CLK_40MHZ
	VA_WREG(0x0e, 1);	// 16bpp
	VA_WREG(0x58, 320);	// pitch, words
	VA_WREG(0x5c, 9);
	VA_WREG(0x06, 640);
	VA_WREG(0x08, 480);
	VA_WREG(0x04, 0);	// scale mode 0
	VA_WREG(0x4e, 1);	// passthrough
}

/*
================
VA2000_SetPan

Point the display at a byte offset inside the framebuffer.  The pan value the
board wants is a 16-bit WORD address relative to the framebuffer start, split
across two registers -- established experimentally by the saku26 port, which
also established the thing that makes shared mode possible at all: these two
registers READ BACK, while the mode registers do not.  Write a mode register
and read it again and you get 0x005a whatever you put there, so a mode cannot
be saved and restored; a pan can.
================
*/
static void VA2000_SetPan (unsigned long byte_offset)
{
	unsigned long	pan;

	pan = byte_offset / 2UL;
	VA_WREG(0x38, (pan >> 16) & 0xffff);
	VA_WREG(0x3a, pan & 0xffff);
}

/*
================
VA2000_SharedArgs

-shared            draw as a guest on whatever already owns the board
-sharedmode WxH    what that owner is running; default 800x600

The geometry has to be told rather than discovered, because the CRTC cannot be
read back.  Get it wrong and the picture is skewed or off-centre -- it cannot
damage anything, because no mode register is written either way.
================
*/
static void VA2000_SharedArgs (void)
{
	char	*spec, *x;
	int	j, w, h;

	if (!COM_CheckParm ("-shared"))
		return;

	va_shared = true;

	j = COM_CheckParm ("-sharedmode");
	if (j && j + 1 < com_argc)
	{
		spec = com_argv[j+1];
		x = strchr (spec, 'x');
		if (x)
		{
			w = atoi (spec);
			h = atoi (x + 1);
			if (w >= BASEWIDTH && h >= BASEHEIGHT && w <= 4096 && h <= 4096)
			{
				va_screen_w = w;
				va_screen_h = h;
			}
			else
				Con_Printf ("VID: -sharedmode %s out of range, using %dx%d\n",
						spec, va_screen_w, va_screen_h);
		}
	}

	va_ox = (va_screen_w - BASEWIDTH) / 2;
	va_oy = (va_screen_h - BASEHEIGHT) / 2;
}

/*
================
VID_SetPalette

Quake's palette is 256 RGB triples, 0-255 per component, which is the range
the board's CLUT registers take -- no scaling, unlike the VGA DAC that
vid_svgalib.c has to shift down to 0-63.

In shared mode the CLUT is NOT written.  It belongs to whoever set the mode,
the board is in a 16-bit direct-colour mode where Quake's palette has no
meaning to the hardware, and writing it is exactly the kind of change that
cannot be undone afterwards -- the point of shared mode is to leave with the
board in the state it was found.  The palette becomes d_8to16table instead,
and the conversion happens in VID_Update.
================
*/
void VID_SetPalette (unsigned char *palette)
{
	int	i, r, g, b;

	memcpy (vid_current_palette, palette, sizeof(vid_current_palette));

	// RGB565, which is the only format xrtg offers.  Built always: it costs
	// 256 iterations at startup and it is what shared mode runs on.
	for (i = 0; i < 256; i++)
	{
		r = palette[i*3+0];
		g = palette[i*3+1];
		b = palette[i*3+2];
		d_8to16table[i] = (unsigned short)
			(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	}

	if (va_regs == (char *)-1 || va_shared)
		return;

	for (i = 0; i < 256; i++)
	{
		VA_WREG(VA2000_PAL_R + i*2, palette[i*3+0]);
		VA_WREG(VA2000_PAL_G + i*2, palette[i*3+1]);
		VA_WREG(VA2000_PAL_B + i*2, palette[i*3+2]);
	}
}

void VID_ShiftPalette (unsigned char *palette)
{
	VID_SetPalette (palette);
}

/*
================
VID_Init
================
*/
void VID_Init (unsigned char *palette)
{
	unsigned long	fb_base;
	int		bsize, zsize, tsize;

	VA2000_SharedArgs ();

	va_fd = open (VA2000_DEVICE, O_RDWR);
	if (va_fd < 0)
		Sys_Error ("VID_Init: cannot open %s (run as root?)", VA2000_DEVICE);

	// The registers are at the bottom of the aperture and the drawing area may
	// be 16 MB up it, so they are two mappings.  Console mode maps the same
	// bytes it always did; only the arithmetic moved.
	va_regs = (char *)mmap ((caddr_t)0, (size_t)VA2000_REGWIN,
				PROT_READ | PROT_WRITE, MAP_SHARED, va_fd, (off_t)0);
	if (va_regs == (char *)-1)
	{
		close (va_fd);
		va_fd = -1;
		Sys_Error ("VID_Init: cannot mmap the %s registers", VA2000_DEVICE);
	}

	if (va_shared)
	{
		// A full screen's worth of 16-bit pixels: Quake draws 320x200 into the
		// middle of it and the rest stays black.  It has to be screen-sized
		// because the pitch register belongs to X and is not being touched.
		fb_base = VA2000_SHARED_BASE;
		va_fbwin_len = (unsigned long)va_screen_w * (unsigned long)va_screen_h * 2UL;
	}
	else
	{
		fb_base = 0UL;
		va_fbwin_len = (unsigned long)BASEWIDTH * (unsigned long)FB_ROWS_FETCHED;
	}
	va_fbwin_len = (va_fbwin_len + 0xFFFFUL) & ~0xFFFFUL;

	va_fbwin = (char *)mmap ((caddr_t)0, (size_t)va_fbwin_len,
				 PROT_READ | PROT_WRITE, MAP_SHARED, va_fd,
				 (off_t)(VA2000_FB_OFFSET + fb_base));
	if (va_fbwin == (char *)-1)
	{
		munmap ((caddr_t)va_regs, (size_t)VA2000_REGWIN);
		va_regs = (char *)-1;
		close (va_fd);
		va_fd = -1;
		Sys_Error ("VID_Init: cannot mmap %ld KB of framebuffer at %ld MB",
				(long)(va_fbwin_len / 1024), (long)(fb_base / 1048576UL));
	}

	va_fb = (byte *)va_fbwin;

	vid.width = vid.conwidth = BASEWIDTH;
	vid.height = vid.conheight = BASEHEIGHT;
	vid.rowbytes = vid.conrowbytes = BASEWIDTH;
	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.numpages = 1;
	vid.colormap = (pixel_t *)host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));

	// Non-square pixels, as on a VGA 320x200: the board scales to 640x400 and
	// the monitor is 4:3.  Same expression id uses in vid_svgalib.c.
	vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);

	// One high-hunk block: z-buffer, then surface cache, then the render
	// buffer.  Quake reads back from vid.buffer, and reads over Zorro II are
	// slow, so the buffer lives in main memory and VID_Update copies it out.
	bsize = vid.rowbytes * vid.height;
	tsize = D_SurfaceCacheForRes (vid.width, vid.height);
	zsize = vid.width * vid.height * sizeof(*d_pzbuffer);

	VID_highhunkmark = Hunk_HighMark ();
	d_pzbuffer = Hunk_HighAllocName (bsize + tsize + zsize, "video");
	if (!d_pzbuffer)
		Sys_Error ("VID_Init: not enough memory for video buffers");

	vid_surfcache = ((byte *)d_pzbuffer) + zsize;
	vid.conbuffer = vid.buffer = (pixel_t *)(((byte *)d_pzbuffer) + zsize + tsize);

	D_InitCaches (vid_surfcache, tsize);

	if (va_shared)
	{
		// Where the display is pointing right now.  This is what gets put
		// back, whatever it is -- an X server's screen, a panned virtual
		// desktop, or the Amiga passthrough buffer.  Restoring what was
		// actually there needs no reasoning about who owns the board and
		// cannot be wrong; the saku26 port reasoned instead, restored pan 0,
		// and blanked the display of every machine that had no X server.
		va_saved_pan_hi = VA_RREG(0x38);
		va_saved_pan_lo = VA_RREG(0x3a);

		Con_Printf ("VID: sharing the board, %dx%d, buffer %ld MB up\n",
				va_screen_w, va_screen_h,
				(long)(VA2000_SHARED_BASE / 1048576UL));
		Con_Printf ("VID: no mode register written; pan on entry 0x%04x%04x\n",
				(int)va_saved_pan_hi, (int)va_saved_pan_lo);

		// 0xf8 in the high half is the Amiga passthrough buffer, which means
		// nothing is driving an RTG screen -- so there is no X server here to
		// be a guest of, and -shared is not what was wanted.  Say so: the
		// alternative is a black screen and nothing to go on.
		if (va_saved_pan_hi == 0xf8)
			Con_Printf ("VID: WARNING -- that is Amiga passthrough, so no X "
					"server appears\nVID: to be running.  Without -shared you "
					"would get a mode set and a\nVID: clean return to the "
					"console.\n");

		// Black the whole buffer before showing it, or the first frame is
		// whatever the board had at 16 MB.
		memset ((char *)va_fb, 0, (size_t)va_fbwin_len);
		VA2000_SetPan (VA2000_SHARED_BASE);

		// Draw_DebugChar writes 8-bit pixels straight at vid.direct, and in
		// this mode the framebuffer is 16-bit.  It already handles a null
		// pointer by doing nothing, which is the right answer here.
		vid.direct = NULL;
	}
	else
	{
		VA2000_SetMode ();

		// vid.direct is real screen memory: D_BeginDirectRect draws the
		// loading disc straight to it, bypassing the render buffer.
		vid.direct = va_fb;

		memset ((char *)va_fb, 0, (size_t)(vid.rowbytes * FB_ROWS_FETCHED));
	}

	VID_SetPalette (palette);
}

/*
================
VID_Shutdown
================
*/
void VID_Shutdown (void)
{
	if (va_regs != (char *)-1)
	{
		if (va_shared)
		{
			// Put the display back exactly where it was pointing.  Its pixels
			// were never written, so it reappears intact and whoever owns it
			// needs no redraw -- which matters, because an X server is not
			// going to be told that any of this happened.
			VA_WREG(0x38, va_saved_pan_hi);
			VA_WREG(0x3a, va_saved_pan_lo);
		}
		else
		{
			VA2000_RestorePassthrough ();
		}

		munmap ((caddr_t)va_regs, (size_t)VA2000_REGWIN);
		va_regs = (char *)-1;
	}

	if (va_fbwin != (char *)-1)
	{
		munmap ((caddr_t)va_fbwin, (size_t)va_fbwin_len);
		va_fbwin = (char *)-1;
	}
	va_fb = NULL;

	if (va_fd >= 0)
	{
		close (va_fd);
		va_fd = -1;
	}
}

/*
================
VA2000_CopyRow

Copy one row to the board in 32-bit units.

Not a micro-optimisation.  AMIX's libc memcpy is a byte loop -- literally
`move.b (a1)+,(a0)+` around a `dbf` -- and the destination here is a Zorro II
board, where every access is a bus cycle.  Byte writes to a 16-bit bus spend
one cycle per byte, so a byte-wise memcpy costs four times the cycles of a
long-word copy for the same data.  Measured over a whole timedemo, the frame
copy was 62 ms of a 204 ms frame.

Both ends are long-aligned in the common case: the framebuffer starts at a
64 KB boundary, rows are 320 bytes, and vid.buffer comes off the hunk.  A
rect whose x is not a multiple of four still has to be handled, hence the
head and tail.
================
*/
static void VA2000_CopyRow (byte *dst, byte *src, int count)
{
	unsigned long	*ld, *ls;
	int		n;

	// Head: bytes up to the first long boundary, and give up on the wide
	// path entirely if the two ends cannot be aligned together.
	if (((long)dst & 3) != ((long)src & 3))
	{
		while (count-- > 0)
			*dst++ = *src++;
		return;
	}

	while (count > 0 && ((long)dst & 3))
	{
		*dst++ = *src++;
		count--;
	}

	ld = (unsigned long *)dst;
	ls = (unsigned long *)src;
	for (n = count >> 2; n > 0; n--)
		*ld++ = *ls++;

	dst = (byte *)ld;
	src = (byte *)ls;
	for (count &= 3; count > 0; count--)
		*dst++ = *src++;
}

/*
================
VA2000_CopyRow16

One row of 8-bit pixels out to a 16-bit framebuffer, through d_8to16table.

Two pixels are packed into one 32-bit write wherever the destination allows
it, for the reason VA2000_CopyRow gives: a 16-bit store to a Zorro board is a
bus cycle, and halving the number of them halves what the row costs.  The
table lookups happen either way; the bus is the part worth saving.

This is the price of shared mode.  The console path writes one byte per pixel
and this writes two, over a bus that is the frame's bottleneck -- so expect
shared mode to be slower, and expect the gap to close to nothing if the
board's colour-mode register ever turns out to be readable, because then the
board can be put in 8bpp and put back.
================
*/
static void VA2000_CopyRow16 (unsigned short *dst, byte *src, int count)
{
	unsigned long	*ld;
	int		n;

	// Odd address: no 32-bit path, and a 16-bit store is all that is legal.
	if ((long)dst & 2)
	{
		while (count-- > 0)
			*dst++ = d_8to16table[*src++];
		return;
	}

	ld = (unsigned long *)dst;
	for (n = count >> 1; n > 0; n--)
	{
		*ld++ = ((unsigned long)d_8to16table[src[0]] << 16)
			| (unsigned long)d_8to16table[src[1]];
		src += 2;
	}

	if (count & 1)
		*((unsigned short *)ld) = d_8to16table[*src];
}

/*
================
VID_Update

Copy the dirty rectangles out to the board.  Honouring the rect list rather
than blitting everything matters here: the board is on Zorro II, and the copy
is a measurable fraction of the frame.
================
*/
void VID_Update (vrect_t *rects)
{
	unsigned short	*dst16;
	byte		*src, *dst;
	int		y;

	if (!va_fb)
		return;

	if (va_shared)
	{
		// The board is in somebody else's mode, so a row is va_screen_w
		// 16-bit pixels wide and Quake's 320x200 sits in the middle of it.
		while (rects)
		{
			src = (byte *)vid.buffer + rects->y * vid.rowbytes + rects->x;
			dst16 = (unsigned short *)va_fb
				+ (va_oy + rects->y) * va_screen_w + va_ox + rects->x;

			for (y = 0; y < rects->height; y++)
			{
				VA2000_CopyRow16 (dst16, src, rects->width);
				src += vid.rowbytes;
				dst16 += va_screen_w;
			}

			rects = rects->pnext;
		}
		return;
	}

	while (rects)
	{
		src = (byte *)vid.buffer + rects->y * vid.rowbytes + rects->x;
		dst = va_fb + rects->y * vid.rowbytes + rects->x;

		for (y = 0; y < rects->height; y++)
		{
			VA2000_CopyRow (dst, src, rects->width);
			src += vid.rowbytes;
			dst += vid.rowbytes;
		}

		rects = rects->pnext;
	}
}

/*
================
D_BeginDirectRect

Draws straight to the board -- this is the loading disc, which has to appear
while the renderer is busy and vid.buffer is not being flushed.
================
*/
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
	unsigned short	*dst16;
	byte		*dst;
	int		row;

	if (!va_fb)
		return;

	if (va_shared)
	{
		dst16 = (unsigned short *)va_fb + (va_oy + y) * va_screen_w + va_ox + x;
		for (row = 0; row < height; row++)
		{
			VA2000_CopyRow16 (dst16, pbitmap, width);
			pbitmap += width;
			dst16 += va_screen_w;
		}
		return;
	}

	dst = va_fb + y * vid.rowbytes + x;
	for (row = 0; row < height; row++)
	{
		memcpy ((char *)dst, (char *)pbitmap, (size_t)width);
		pbitmap += width;
		dst += vid.rowbytes;
	}
}

/*
================
D_EndDirectRect

Put back what the render buffer holds for that area.
================
*/
void D_EndDirectRect (int x, int y, int width, int height)
{
	unsigned short	*dst16;
	byte		*src, *dst;
	int		row;

	if (!va_fb || !vid.buffer)
		return;

	src = (byte *)vid.buffer + y * vid.rowbytes + x;

	if (va_shared)
	{
		dst16 = (unsigned short *)va_fb + (va_oy + y) * va_screen_w + va_ox + x;
		for (row = 0; row < height; row++)
		{
			VA2000_CopyRow16 (dst16, src, width);
			src += vid.rowbytes;
			dst16 += va_screen_w;
		}
		return;
	}

	dst = va_fb + y * vid.rowbytes + x;
	for (row = 0; row < height; row++)
	{
		memcpy ((char *)dst, (char *)src, (size_t)width);
		src += vid.rowbytes;
		dst += vid.rowbytes;
	}
}
