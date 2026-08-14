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


