#if defined(OF_BOOT_LOG) && !defined(OF_PC)

#include "of_ecwolf_bootlog.h"
#include "of_file.h"
#include "of_timer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The whole log is kept in RAM and rewritten to the slot file on every
 * append, so each line is persisted through the same save write-back path
 * the game uses and a hard hang still leaves every prior line on the card. */
static char of_bootlog_buf[16384];
static unsigned of_bootlog_len;
static unsigned of_bootlog_seq;
static int of_bootlog_registered;

void OF_BootLog(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    int n, m;
    FILE *f;

    if (!of_bootlog_registered)
    {
        of_file_slot_register(19, "ofbootlog.sav");
        of_bootlog_registered = 1;
    }

    n = snprintf(line, sizeof(line), "%03u %8lu ",
                 of_bootlog_seq++, (unsigned long)of_time_ms());
    if (n < 0)
        n = 0;
    va_start(ap, fmt);
    m = vsnprintf(line + n, sizeof(line) - n, fmt, ap);
    va_end(ap);
    if (m < 0)
        m = 0;
    if ((unsigned)(n + m) >= sizeof(line))
        m = (int)sizeof(line) - 1 - n;
    if (m == 0 || line[n + m - 1] != '\n')
    {
        line[n + m] = '\n';
        m++;
    }
    if (of_bootlog_len + (unsigned)(n + m) <= sizeof(of_bootlog_buf))
    {
        memcpy(of_bootlog_buf + of_bootlog_len, line, (unsigned)(n + m));
        of_bootlog_len += (unsigned)(n + m);
    }

    f = fopen("ofbootlog.sav", "wb");
    if (f == NULL)
        return;
    fwrite(of_bootlog_buf, 1, of_bootlog_len, f);
    fclose(f);
}

#else
typedef int of_ecwolf_bootlog_unused;
#endif
