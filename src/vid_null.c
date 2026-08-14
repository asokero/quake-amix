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
// vid_null.c -- null video driver to aid porting efforts

#include "quakedef.h"
#include "d_local.h"

viddef_t	vid;				// global video state

#define	BASEWIDTH	320
#define	BASEHEIGHT	200

unsigned short	d_8to16table[256];
unsigned	d_8to24table[256];

static int	VID_highhunkmark;
static byte	*vid_surfcache;

void	VID_SetPalette (unsigned char *palette)
{
}

void	VID_ShiftPalette (unsigned char *palette)
{
}

void	VID_Init (unsigned char *palette)
{
	int	bsize, zsize, tsize;

	vid.maxwarpwidth = vid.width = vid.conwidth = BASEWIDTH;
	vid.maxwarpheight = vid.height = vid.conheight = BASEHEIGHT;
	vid.numpages = 1;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
	vid.rowbytes = vid.conrowbytes = BASEWIDTH;

	// Same aspect expression the real driver uses, so that a timedemo run
	// here and one through the VA2000 differ only by the display.
	vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);

	// id's stub allocates a fixed 256 KB surface cache.  That is well under
	// the 600 KB D_SurfaceCacheForRes wants at 320x200, and an undersized
	// cache thrashes -- surfaces get flushed and rebuilt that would otherwise
	// have been kept.  It costs frames, which makes the stub useless as a
	// baseline: a comparison against the VA2000 build would be measuring the
	// cache size as much as the display.  Allocate what the renderer asks
	// for, exactly as vid_va2000.c does.
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
}

void	VID_Shutdown (void)
{
}

void	VID_Update (vrect_t *rects)
{
#ifdef VID_CHECKSUM
	// Does an optimisation change what gets drawn, or only how fast?  A
	// timedemo cannot say -- it reports frames and seconds, and identical
	// frame counts prove nothing about pixels.  Demo playback is
	// deterministic, so comparing frames between two builds does.
	//
	// The checksum is PER FRAME, not cumulative.  A running total was tried
	// first and was worse than useless: frames drawn while the map loads show
	// whatever console text had been printed by then, which is timing
	// dependent, so one early difference poisons every later value and two
	// builds that render the demo identically still disagree everywhere.
	//
	// Cheap and order-sensitive rather than a real CRC; it only has to catch
	// a difference, not resist one.  VID_DUMP_FRAME below is the tiebreaker:
	// a checksum says two builds differ, a dumped frame says by how many
	// pixels -- a handful means floating-point rounding moved a texture
	// coordinate, tens of thousands means the renderer is broken.
	static int		frame;
	unsigned long		sum;
	unsigned char		*p;
	int			i, n;

	p = (unsigned char *)vid.buffer;
	n = vid.rowbytes * vid.height;
	sum = 0;
	for (i = 0; i < n; i++)
		sum = (sum << 1) + (sum >> 31) + p[i];

	frame++;
	if ((frame % 200) == 0)
		Sys_Printf ("VIDSUM frame %ld = %08lx\n", (long)frame, sum);

	// A checksum says two builds differ; it cannot say by how much, and
	// floating-point optimisation can legitimately move a texture coordinate
	// by one ULP and with it a pixel.  Dumping one frame lets the difference
	// be counted instead of guessed: a handful of pixels is rounding, tens of
	// thousands is a broken renderer.
	if (frame == VID_DUMP_FRAME)
	{
		int	h;

		h = Sys_FileOpenWrite ("frame.raw");
		if (h != -1)
		{
			Sys_FileWrite (h, vid.buffer, n);
			Sys_FileClose (h);
			Sys_Printf ("VIDDUMP frame %ld written, %ld bytes\n",
				    (long)frame, (long)n);
		}
		Sys_Quit ();
	}
#endif
}

/*
================
D_BeginDirectRect
================
*/
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
}


/*
================
D_EndDirectRect
================
*/
void D_EndDirectRect (int x, int y, int width, int height)
{
}


