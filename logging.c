#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#define LOGLB_SIZE 1024

#ifdef LOGFILE

static char LogLineBuf[LOGLB_SIZE + 1];
static int LogLBPos = 0;

static FILE *LogFile;

static char *
GetTime()
{
  time_t Time;
  static char TimeBuf[128];
  char *DstPtr, *SrcPtr;

  time(&Time);
  SrcPtr = ctime(&Time);
  DstPtr = TimeBuf;
  while(*SrcPtr >= 32)
    *DstPtr++ = *SrcPtr++;
  *DstPtr = 0;

  return TimeBuf;
}
#endif

void
StartLog()
{
#ifdef LOGFILE
  LogFile = fopen(LOGFILE, "a");
  if (LogFile == 0) {
    perror("Cannot open logfile");
    exit(1);
  }

  fprintf(LogFile, "\n\n%s %5d ---------- MDTerm started by %s (UID %d) ----------\n",
      GetTime(), getpid(), getenv("USER"), getuid());
  fflush(LogFile);
#endif
}

void
LogChar(char C)
{
#ifdef LOGFILE
  if (C == 8) {
    if (LogLBPos > 0)
      LogLBPos--;
  }
  else {
    if (C == 13) {
      /* Send line */
      LogLineBuf[LogLBPos] = 0;
      fprintf(LogFile, "%s %5d %s\n", GetTime(), getpid(), LogLineBuf); fflush(LogFile);
      LogLBPos = 0;
    }
    else
      if (C >= 32)
        if (LogLBPos < LOGLB_SIZE)
	  LogLineBuf[LogLBPos ++] = C;
  }
#endif
}
