/* Very primitive emulator of Commodore 65 + sub-set (!!) of Mega65 fetures.
   Copyright (C)2016,2017 LGB (Gábor Lénárt) <lgblgblgb@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef __XEMU_VIC3_MEGA65_H_INCLUDED
#define __XEMU_VIC3_MEGA65_H_INCLUDED

#define VIC2_IOMODE 0
#define VIC3_IOMODE 1
#define VIC_BAD_IOMODE 2
#define VIC4_IOMODE 3

extern int   vic_iomode;
extern int   scanline;
extern Uint8 vic3_registers[];
extern int   cpu_cycles_per_scanline;
extern int   vic2_16k_bank;
extern int   vic3_blink_phase;
extern int   force_fast;
extern Uint8 c128_d030_reg;

extern void  vic3_init ( void );
extern void  vic3_write_reg ( int addr, Uint8 data );
extern Uint8 vic3_read_reg ( int addr );
extern void  vic3_write_palette_reg ( int num, Uint8 data );
extern void  vic3_render_screen ( void );
extern void  vic3_check_raster_interrupt ( void );
extern void  machine_set_speed ( int verbose );

#ifdef XEMU_SNAPSHOT_SUPPORT
#include "xemu/emutools_snapshot.h"
extern int vic4_snapshot_load_state ( const struct xemu_snapshot_definition_st *def , struct xemu_snapshot_block_st *block );
extern int vic4_snapshot_save_state ( const struct xemu_snapshot_definition_st *def );
#endif

#endif
