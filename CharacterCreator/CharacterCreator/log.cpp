#include "pch.h"
#include "log.h"

#include <share.h>
#include <stdarg.h>
#include <stdio.h>

static FILE* g_log = NULL;
static SRWLOCK g_logLock = SRWLOCK_INIT;

void LogOpen(const char* path)
{
    // The last session's log is kept (CharacterCreator.previous.log), so the
    // log of a session that crashed survives the next start.
    char previous[MAX_PATH];
    sprintf_s(previous, "%.*s.previous.log", (int)(strlen(path) - 4), path);
    MoveFileExA(path, previous, MOVEFILE_REPLACE_EXISTING);

    g_log = _fsopen(path, "w", _SH_DENYNO);
}

void Log(const char* fmt, ...)
{
    if (!g_log)
        return;

    AcquireSRWLockExclusive(&g_logLock);

    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d  ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);

    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);

    fputc('\n', g_log);
    fflush(g_log);

    ReleaseSRWLockExclusive(&g_logLock);
}
