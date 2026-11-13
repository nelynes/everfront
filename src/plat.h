#ifndef PLAT_H
#define PLAT_H
// The cart under EverFront: files on the SD card, the clock. One file open
// at a time, like the EverDrive's own API.
//   plat_pro.c        EverDrive GBA Pro (its MCU's file API)
//   mini/plat_mini.c  EverDrive GBA X5 Mini (FatFs over raw SD access)
#include "ed/sys.h"

void plat_init(void);                         // once, first thing
const char *plat_name(void);                  // "Pro", "Mini"

// A directory's entries one at a time, in no particular order.
u8 plat_dir_open(const char *path);           // 0 = ok
u8 plat_dir_next(char *name, u32 max, u8 *is_dir);   // 0 = an entry, 1 = no more, else an error
// Pro only: the listing stays on the cart, sorted; entries by index.
u16 plat_dir_pos(void);                       // index of the entry dir_next just gave
u8 plat_dir_name(u16 idx, char *name, u32 max);

u8 plat_open(const char *path, u8 write);     // write: create or replace
u8 plat_read(void *dst, u32 len);             // dst 4-byte aligned
u8 plat_seek(u32 off);
u32 plat_size(void);
u8 plat_write(const void *src, u32 len);
void plat_close(void);

u8 plat_time(u8 t[5]);                        // year, month, day, hour, minute (BCD); 0 = ok

#endif
