// Fake EverDrive GBA X5 Mini for emulator tests (never on a cart): the SD
// card is a FAT image appended to the emulator image at 0x08100000.
#include "../src/edmini/sys.h"
#define IMG ((const u8 *) 0x08100000)

u8 diskInit() { return 0; }
u8 diskRead(u32 sec, u8 *dst, u16 n) {
    if (((u32) dst >> 24) == 8) return 0;           // "loading into PSRAM": the emulator's rom can't change
    const u16 *s = (const u16 *) (IMG + sec * 512);
    u16 *d = (u16 *) dst;
    for (u32 i = 0; i < (u32) n * 256; i++) d[i] = s[i];
    return 0;
}
void diskPowerDown() {}
u8 ed_init() { return 0; }
u16 ed_get_fpga_ver() { return 0x1234; }
void ed_rtc_on() {}
void ed_rtc_off() {}
void ed_set_save_type(u8 t) { (void) t; }
void ed_set_rom_mask(u32 s) { (void) s; }
void ed_set_rom_bank(u8 b) { (void) b; }
void ed_lock_regs() {}
