// FatFs disk glue for the EverDrive GBA X5 Mini: krikzz's SD driver
// (edmini/disk.c). Read-only build: no write, no sync needed.
#include "../fatfs/ff.h"
#include "../fatfs/diskio.h"
#include "../edmini/sys.h"
#include "../edmini/disk.h"

static DSTATUS stat = STA_NOINIT;

DSTATUS disk_status(BYTE pdrv) { return pdrv ? STA_NOINIT : stat; }

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    stat = diskInit() ? STA_NOINIT : 0;
    return stat;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv || (stat & STA_NOINIT)) return RES_NOTRDY;
    while (count) {
        UINT n = count > 0x8000 ? 0x8000 : count;
        if (diskRead(sector, buff, n)) return RES_ERROR;
        sector += n;
        buff += n * 512;
        count -= n;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    (void) buff;
    if (pdrv) return RES_PARERR;
    return cmd == CTRL_SYNC ? RES_OK : RES_PARERR;
}
