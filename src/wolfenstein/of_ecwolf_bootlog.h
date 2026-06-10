#ifndef OF_ECWOLF_BOOTLOG_H
#define OF_ECWOLF_BOOTLOG_H

/* File-backed boot trace (`make DEBUG_BOOTLOG=1` -> -DOF_BOOT_LOG): appends
 * breadcrumb lines to save slot 19 and rewrites the whole log on every call,
 * so the last persisted line marks how far boot got even on a black screen.
 * Read it back from the card as the slot's bound save file. */

#if defined(OF_BOOT_LOG) && !defined(OF_PC)
#ifdef __cplusplus
extern "C"
#endif
void OF_BootLog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#else
#define OF_BootLog(...) ((void)0)
#endif

#endif
