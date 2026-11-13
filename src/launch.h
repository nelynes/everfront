#ifndef LAUNCH_H
#define LAUNCH_H

#include "ed/sys.h"

// The app's folder at the SD card's root: themes, label packs, settings, log.
#define APP_DIR "everfront"

// Status callback while a game is being prepared (short text for the screen).
typedef void (*LaunchStatus)(const char *msg);

// Once at start: firmware check, in-game-menu pieces from the resident menu.
void launch_init(void);
// 1 if the stock in-game menu can be installed for our launches.
u8 launch_igm_available(void);

// Prepares and boots the rom at path (as the cart wants it, e.g.
// "/Game Boy Advance/x.gba"; file_name is the bare file name).
// Returns only on error, with the error code (and a message in *err).
u8 launch_game(const char *path, const char *file_name, LaunchStatus status, const char **err);
// A game for another system (ext "gb", "gbc", "nes"...) through the
// EverDrive's emulator in edgba/edapp/<ext>/.
u8 launch_app(const char *path, const char *file_name, const char *ext, LaunchStatus status, const char **err);

// Log to everfront/log.txt (buffered; flushed by log_flush).
void lgq(const char *fmt, ...);
u8 log_flush(void);
// At start-up: keeps log.txt from growing past 64 KB (keeps its newest 16 KB).
void log_trim(void);
// The log kept in memory (the newest part), for the on-screen viewer.
const char *log_text(u32 *len);

u16 vsprintf_ed(char *buf, const char *fmt, __builtin_va_list ap);

#endif
