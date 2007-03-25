/* filter.c - filtering of echange output by external program */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

#include "ewterm.h"


char FilterCmd[256] = "";

int FilterFdIn = -1, FilterFdOut = -1;

char *FilterQueue = NULL;
int FilterQueueLen = 0;
struct termios tio;

void
InitFilter()
{
  int filter_stdin[2], filter_stdout[2];
  if (!*FilterCmd) return;
  
  /* We have a filter to run, so exec() it, kidnapping its stdin/stdout. */
  /* TODO: Catch stderr as well and show it in error colors. Would there be
   * a danger of mixing stdout and stderr in the output window w/o complicated
   * buffering? --pasky */

  if (pipe(filter_stdin) < 0 || pipe(filter_stdout) < 0) {
    sprintf(MultiBuf, "Filter pipe can't be deployed: %s\n", strerror(errno));
    AddEStr(MultiBuf, 0, 0);
    return;
  }

  FilterFdIn = filter_stdin[1];
  FilterFdOut = filter_stdout[0];
#if 1
  tcgetattr(FilterFdIn, &tio);
  cfmakeraw(&tio);
  tcsetattr(FilterFdIn, TCSANOW, &tio);
  tcgetattr(FilterFdOut, &tio);
  cfmakeraw(&tio);
  tcsetattr(FilterFdOut, TCSANOW, &tio);
#endif
  switch (fork()) {
    case -1:
      sprintf(MultiBuf, "Couldn't fork the filter process: %s\n", strerror(errno));
      AddEStr(MultiBuf, 0, 0);
      return;
    case 0:
      break;
    default:
      /* It's up to the child from now on. If something will fail, we'll see
       * it in the next select as the pipe will die. Hopefully. */
      close(filter_stdin[0]);
      close(filter_stdout[1]);
      return;
  }

  /* This is the child process' code path already. */

  close(filter_stdin[1]);
  close(filter_stdout[0]);

  {
    int i;

    /* Clean up the fd table. */
    
    for (i = 0; i < 256; i++)
      if (i != filter_stdin[0] && i != filter_stdout[1]
	  && i != 2 /* XXX: Give them normal stderr for now. */)
	close(i);
  }

  if (dup2(filter_stdin[0], 0) < 0 || dup2(filter_stdout[1], 1) < 0) {
    perror("filter: dup2");
    exit(1);
  }

  exit(system(FilterCmd));
  /* wzzzzzzzzzzzzzzt */
}

void
DoneFilter()
{
  close(FilterFdIn);
  close(FilterFdOut);
}

void
AddToFilterQueue(char Chr)
{
  if (!(FilterQueueLen%256))
    FilterQueue = realloc(FilterQueue, FilterQueueLen + 256);
  FilterQueue[FilterQueueLen++] = Chr;
}
