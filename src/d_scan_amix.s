| Copyright (C) 1996-1997 Id Software, Inc.
| Copyright (C) NovaCoder (AmiQuake 68k assembly)
|
| This program is free software; you can redistribute it and/or
| modify it under the terms of the GNU General Public License
| as published by the Free Software Foundation; either version 2
| of the License, or (at your option) any later version.
|
| This program is distributed in the hope that it will be useful,
| but WITHOUT ANY WARRANTY; without even the implied warranty of
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
|
| See the GNU General Public License for more details.
|
| You should have received a copy of the GNU General Public License
| along with this program; if not, write to the Free Software
| Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
|
| d_scan_amix.s -- D_DrawTurbulent8Span in 68k assembly, for AMIX SVR4
|
| Converted from d_scan_68k.s in the GCC/m68k-amigaos AmiQuake port, which
| disassembled it out of NovaCoder's AmiQuake v1.36 binary.  That file is in
| vasm Motorola syntax and targets AmigaOS; this one is GNU as, SVR4 flavour.
|
| WHAT WAS NOT CONVERTED, AND WHY
|
| The same source file also contains a D_DrawSpans8, and it is not here
| because it does not implement what d_scan.c's D_DrawSpans8 does:
|
|   * the C recomputes the perspective every 8 pixels -- sdivz, tdivz, zi, z,
|     snext, tnext, and fresh sstep/tstep per segment.  The assembly computes
|     one sstep for a whole span, so long spans would visibly warp.
|   * it has no tstep at all.
|   * worse, it destroys t: the inner loop keeps t in %d3 and then loads the
|     fetched texel into %d3.  From the second pixel of every span onward, the
|     t coordinate is the previous pixel's colour index.
|   * it reads and writes the z-buffer, which D_DrawSpans8 does not touch, and
|     tests "transparency" against a register overwritten two instructions
|     earlier.
|
| A faithful conversion would therefore have produced a faster renderer that
| draws the wrong thing.  Writing a correct one from the C is worthwhile -- the
| span drawer is the hottest loop in the game -- but it is new work, not a
| translation.
|
| This routine, by contrast, matches its C original instruction for
| instruction, and is a real cost: every liquid surface goes through it.
|
| SYNTAX NOTES, for whoever converts the next one
|
|   * immediates are `&5`, not `#5` -- this gas takes the SVR4 spelling
|   * registers carry `%`
|   * no underscore on C symbols: AMIX SVR4 ELF has none, unlike AmigaOS
|   * XDEF/XREF/section/end become .globl/.text; undefined symbols are
|     external automatically
|   * assemble with `as` directly, NOT through the gcc wrapper: that wrapper
|     rewrites compare operands to compensate for a gcc quirk, and would
|     reverse hand-written ones.  See docs/TOOLCHAIN.md §1.

	.text
	.globl	D_DrawTurbulent8Span

| void D_DrawTurbulent8Span (void)
| {
|     do {
|         sturb = ((r_turb_s + r_turb_turb[(r_turb_t>>16)&(CYCLE-1)])>>16)&63;
|         tturb = ((r_turb_t + r_turb_turb[(r_turb_s>>16)&(CYCLE-1)])>>16)&63;
|         *r_turb_pdest++ = *(r_turb_pbase + (tturb<<6) + sturb);
|         r_turb_s += r_turb_sstep;
|         r_turb_t += r_turb_tstep;
|     } while (--r_turb_spancount > 0);
| }
|
| r_turb_spancount is deliberately not written back.  The C decrements the
| global to zero, but its only caller in d_scan.c reassigns it at the top of
| every iteration and never reads it in between.

D_DrawTurbulent8Span:
	movem.l	%d2-%d7/%a2-%a3,-(%sp)

	| Globals into registers; the loop then touches memory only for the
	| sine table and the two texture accesses.
	move.l	r_turb_s,%d2		| s, 16.16 fixed point
	move.l	r_turb_t,%d3		| t, 16.16
	move.l	r_turb_sstep,%d4
	move.l	r_turb_tstep,%d5
	move.l	r_turb_spancount,%d7	| loop counter
	move.l	r_turb_pbase,%a0	| texture base
	move.l	r_turb_pdest,%a1	| destination
	move.l	r_turb_turb,%a2		| sine table

	moveq	&127,%d6		| CYCLE-1

.Lturb_pixel:
	| sturb = ((s + turb[(t>>16)&127])>>16)&63
	move.l	%d3,%d0
	swap	%d0			| t >> 16
	and.l	%d6,%d0			| & (CYCLE-1)
	lsl.l	&2,%d0			| int index -> byte offset
	move.l	(%a2,%d0.l),%d0		| turb[...]
	add.l	%d2,%d0
	swap	%d0			| >> 16
	and.w	&63,%d0			| sturb

	| tturb = ((t + turb[(s>>16)&127])>>16)&63
	move.l	%d2,%d1
	swap	%d1
	and.l	%d6,%d1
	lsl.l	&2,%d1
	move.l	(%a2,%d1.l),%d1
	add.l	%d3,%d1
	swap	%d1
	and.w	&63,%d1			| tturb

	| *pdest++ = pbase[(tturb<<6) + sturb]
	lsl.w	&6,%d1
	add.w	%d0,%d1
	move.b	(%a0,%d1.w),%d0
	move.b	%d0,(%a1)+

	add.l	%d4,%d2			| s += sstep
	add.l	%d5,%d3			| t += tstep

	subq.l	&1,%d7
	bgt.s	.Lturb_pixel

	| Write back what the caller reads next.
	move.l	%d2,r_turb_s
	move.l	%d3,r_turb_t
	move.l	%a1,r_turb_pdest

	movem.l	(%sp)+,%d2-%d7/%a2-%a3
	rts
