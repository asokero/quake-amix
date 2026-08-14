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

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
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
#define	VA2000_BOARD_SIZE	0x00400000UL
#define	VA2000_FB_OFFSET	0x00010000UL

// Palette CLUT registers: one 16-bit register per component per index.
#define	VA2000_PAL_R		0x200
#define	VA2000_PAL_G		0x400
#define	VA2000_PAL_B		0x600

#define	VA_WREG(off,val) \
	(*((volatile unsigned short *)(va_board + (off))) = (unsigned short)(val))

static int	va_fd = -1;
static char	*va_board = (char *)-1;		// mmap base, (char *)-1 when unmapped
static byte	*va_fb = NULL;			// board + VA2000_FB_OFFSET

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
VID_SetPalette

Quake's palette is 256 RGB triples, 0-255 per component, which is the range
the board's CLUT registers take -- no scaling, unlike the VGA DAC that
vid_svgalib.c has to shift down to 0-63.
================
*/
void VID_SetPalette (unsigned char *palette)
{
	int	i;

	memcpy (vid_current_palette, palette, sizeof(vid_current_palette));

	if (va_board == (char *)-1)
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
	int	bsize, zsize, tsize;

	va_fd = open (VA2000_DEVICE, O_RDWR);
	if (va_fd < 0)
		Sys_Error ("VID_Init: cannot open %s (run as root?)", VA2000_DEVICE);

	va_board = (char *)mmap ((caddr_t)0, (size_t)VA2000_BOARD_SIZE,
				 PROT_READ | PROT_WRITE, MAP_SHARED, va_fd, (off_t)0);
	if (va_board == (char *)-1)
	{
		close (va_fd);
		va_fd = -1;
		Sys_Error ("VID_Init: cannot mmap %s", VA2000_DEVICE);
	}

	va_fb = (byte *)(va_board + VA2000_FB_OFFSET);

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

	VA2000_SetMode ();
	VID_SetPalette (palette);

	// vid.direct is real screen memory: D_BeginDirectRect draws the loading
	// disc straight to it, bypassing the render buffer.
	vid.direct = va_fb;

	memset ((char *)va_fb, 0, (size_t)(vid.rowbytes * FB_ROWS_FETCHED));
}

/*
================
VID_Shutdown
================
*/
void VID_Shutdown (void)
{
	if (va_board != (char *)-1)
	{
		VA2000_RestorePassthrough ();
		munmap ((caddr_t)va_board, (size_t)VA2000_BOARD_SIZE);
		va_board = (char *)-1;
		va_fb = NULL;
	}

	if (va_fd >= 0)
	{
		close (va_fd);
		va_fd = -1;
	}
}

/*
================
VID_Update

Copy the dirty rectangles out to the board.  Zorro II runs at roughly
3.5 MB/s, so a full 320x200 frame costs on the order of 20 ms in bus time
alone -- honouring the rect list rather than blitting everything is worth
real frames here.
================
*/
void VID_Update (vrect_t *rects)
{
	byte	*src, *dst;
	int	y;

	if (!va_fb)
		return;

	while (rects)
	{
		src = (byte *)vid.buffer + rects->y * vid.rowbytes + rects->x;
		dst = va_fb + rects->y * vid.rowbytes + rects->x;

		for (y = 0; y < rects->height; y++)
		{
			memcpy ((char *)dst, (char *)src, (size_t)rects->width);
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
	byte	*dst;
	int	row;

	if (!va_fb)
		return;

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
	byte	*src, *dst;
	int	row;

	if (!va_fb || !vid.buffer)
		return;

	src = (byte *)vid.buffer + y * vid.rowbytes + x;
	dst = va_fb + y * vid.rowbytes + x;
	for (row = 0; row < height; row++)
	{
		memcpy ((char *)dst, (char *)src, (size_t)width);
		src += vid.rowbytes;
		dst += vid.rowbytes;
	}
}
