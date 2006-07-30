/*
 * All includes ever needed during long and strange ways
 * of ewterm's/mdterm's life
 */

#include <stdio.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/un.h>
#include <signal.h>
#include <errno.h>
#include <panel.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include "version.h"
#include "ewterm.h"
#include "ascii.h"
#include "iproto.h"


static char *wl = "4z"; /* don't delete this */

int MainLoop = 0;

char HostName[256] = "localhost", HostPortStr[256] = "";
unsigned int HostPort = 7880;

char BurstLinesStr[256] = "";
int BurstLines = -1;

int Reconnect = 0;


/* Functions */

void Done(int Err) {
	doupdate();

	if (CUAWindow) SaveHistory();

	if (!LoggedOff && connection->fwmode != FWD_IN) {
		if (!Err) {
			AddEStr("You need to log yourself off before quit!\n", 0, 0);
			return;
		} else {
			AddEStr("Error happenned and I can't log you off!\n", 0, 0);
			sleep(2);
		}
	}

	DoneFilter();

	if (CUAPanel) del_panel(CUAPanel);
	if (CUAWindow) delwin(CUAWindow);
	if (InfoPanel) del_panel(InfoPanel);
	if (InfoWindow) delwin(InfoWindow);

	clear();
	refresh();
	wait(0);
	resetty();
	/* if (!Err) refresh(); */
	endwin();

	exit(Err);
}

void DoneQuit() {
	Done(0);
}

void SigIntCaught() {
	/* debug purposes ;-) */
}

void SigChldCaught() {
  int status = 0;

  if (wait(&status) >= 0 && WIFEXITED(status) && WEXITSTATUS(status)) {
	  char s[256];

	  snprintf(s, 256, "Error: child exited with error code %d!\n",
				WEXITSTATUS(status));
	  /* XXX: Reentrancy? */
	  AddEStr(s, 0, 0);
  }
  /* Restore handles */
  signal(SIGCHLD, SigChldCaught);
}

void SigTermCaught() {
	Done(0);
	signal(SIGTERM, SigTermCaught);
}

void SigAlrmCaught() {
	signal(SIGALRM, SigAlrmCaught);
	alarm(1);
}

void Init() {
#ifdef DEBUG
	debugf = stderr;
	pdebug("\n\n\n\n\n\n\n\n==================================\n\n\n\n\n\n\n\n");
#endif

	EditHook = BufHook;
	ConvertMode |= CV2CAPS; /* Ehm. */
	signal(SIGTERM, SigTermCaught);
	signal(SIGQUIT, SigTermCaught);
	signal(SIGINT, SigIntCaught);
	signal(SIGALRM, SigAlrmCaught);
	signal(SIGCHLD, SigChldCaught);

	InitScr();
}

void MainProc() {
  ActMenu = (void *)&MainMenu;
  TabHook = Dummy;
  EnterHook = CmdEnterHook;
  UpHook = PrevHistoryCommand;
  DownHook = NextHistoryCommand;
  PgUpHook = DefPgUpHook;
  PgDownHook = Dummy;
  HomeHook = EditHome;
  EndHook = EditEnd;
  EditBuf = LineBuf;
  LineBSize = 255;
  LineWSize = COLS;
  EditXOff = 0;

  RecreateWindows();
  
  AddStr("EWTerm "VERSION" written by Petr Baudis, 2001, 2002, 2003\n\n", 0, 0);
  AddStr("Press F1 for help\n\n", 0, 0);
  
  if (BadOpt) AddEStr("WARNING: Couldn't read option file: Bad version\n\n", 0, 0);

  LoadHistory();
  InitFilter();
  
  alarm(1);
  for (;;) {
    void draw_form_windows();
    int MaxFd;
    fd_set ReadQ;
    fd_set WriteQ;

    MainLoop = 1;
  
    if ((!(DisplayMode & OTHERWINDOW) || (DisplayMode & HELPSHOWN)) && (UpdateCUAW)) {
      wnoutrefresh(CUAWindow);
      UpdateCUAW = 0;
    }

    RedrawStatus(); /* clock; we get here by the alarm(1) every second */

    /* If HELPSHOWN but not active, we need to draw ActEditWindow as last! */
    if ((ShadowHelp || !(DisplayMode & OTHERWINDOW)) && (DisplayMode & HELPSHOWN)) draw_form_windows();
    
    if (!(DisplayMode & NOEDIT)) RefreshEdit();
    wmove(ActEditWindow, EditYOff, LineBPos+EditXOff-LineWOff);
    wnoutrefresh(ActEditWindow);

    if (!ShadowHelp && ((DisplayMode & OTHERWINDOW) && (DisplayMode & HELPSHOWN))) draw_form_windows();

    doupdate();

    MainLoop = 0;

    /* prepare for select */

    if (Reconnect) {
      close(connection->Fd);
      FreeConnection(connection);
      /* To prevent floods. I know, this should be rather handled by ewrecv, but that would be nontrivial. */
      sleep(2);
      AttachConnection();
      Reconnect = 0;
    }

    MaxFd = 0;
    
    FD_ZERO(&ReadQ);
    FD_ZERO(&WriteQ);
    
    FD_SET(0, &ReadQ);

    if (connection) {
      FD_SET(connection->Fd, &ReadQ);
      if (connection->Fd > MaxFd) MaxFd = connection->Fd;
      if (connection->WriteBuffer) FD_SET(connection->Fd, &WriteQ);
    }

    if (FilterFdOut >= 0) {
      FD_SET(FilterFdOut, &ReadQ);
      if (FilterFdOut > MaxFd) MaxFd = FilterFdOut;
    }
    if (FilterFdIn >= 0 && FilterQueueLen) {
      FD_SET(FilterFdIn, &WriteQ);
      if (FilterFdIn > MaxFd) MaxFd = FilterFdIn;
    }

    /* select */

    if (select(MaxFd + 1, &ReadQ, &WriteQ, 0, 0) < 0) {
      char estr[256];

      if (errno == EINTR) continue; /* alarm */
      sprintf(estr, "Select failed: %s\n", strerror(errno));
      AddEStr(estr, 0, 0);
      sleep(1);
      Done(1);
    } else {
      /* User input */
      if (FD_ISSET(0, &ReadQ)) {
	int Key = 0;

	Key = wgetch(ActEditWindow);
	if (Key == ERR) {
	  usleep(50000);
	} else {
	  static int EscPressed = 0;

	  if (Key == 127) Key = 8;
	  if ((Key == 27) && (!EscPressed)) EscPressed = 1;
	  else {
	    if (EscPressed) {
	      Key = ProcessEscKey(Key);
	      EscPressed = 0;
	    }
	    if (Key != 0) {
	      if ((DisplayMode & OTHERWINDOW) && (!(DisplayMode & HELPSHOWN) || ShadowHelp)) BufHook(Key);
	      else EditHook(Key);
	    }
	  }
	}
      }

      /* Exchange input */
      
      if (connection && FD_ISSET(connection->Fd, &ReadQ)) {
	errno = 0;
	if (DoRead(connection) <= 0) {
	  char estr[256];
	  sprintf(estr, "Read from fd failed: %s\n", strerror(errno));
	  AddEStr(estr, 0, 0);
	  sleep(1);
	  Done(1);
	}
	else {
	  int Chr;

	  while (Read(connection, &Chr, 1)) {
	    pdebug("MainProc() proc char %c\n", Chr);

	    /* check if Chr hasn't any special meaning */
	    TestIProtoChar(connection, Chr);
	    
	    if (! (DisplayMode & OTHERWINDOW)) RedrawStatus();
	  }
	}
      }

      /* Exchange output */
      
      if (connection && FD_ISSET(connection->Fd, &WriteQ)) {
	if (DoWrite(connection) < 0) {
	  char estr[256];
	  sprintf(estr, "Write to fd failed: %s\n", strerror(errno));
	  AddEStr(estr, 0, 0);
	  sleep(1);
	  Done(1);
	}
      }

      /* Filter input */
      
      if (FilterFdOut >= 0 && FD_ISSET(FilterFdOut, &ReadQ)) {
	char Buf[256]; int BufLen;

	if ((BufLen = read(FilterFdOut, Buf, 255)) < 0) {
	  char estr[256];
	  sprintf(estr, "Read from fd failed: %s\n", strerror(errno));
	  AddEStr(estr, 0, 0);
	  sleep(1);
	  Done(1);
	}
	else {
	  int Pos = 0, Chr;

	  Buf[BufLen] = 0;

	  while ((Chr = Buf[Pos++])) {
	    static int carrot;

	    pdebug("MainProc() proc filter char (carrot %d) %d : =%c=\n", carrot, Chr, Chr);

	    if (carrot) {
	      switch (Chr) {
		case 'T': ColTerm(); break;
		case 'P': ColPrompt(); break;
		case 'C': ColCmd(); break;
		case '^':
		default :
			  AddCh(Chr);
			  break;
	      }
	      carrot = 0;
	      continue;
	    }

	    if (Chr == '^') {
	      carrot = 1;
	      continue;
	    }

	    AddCh(Chr);
	  }
	}
      }

      /* Exchange output */
      
      if (FilterFdIn >= 0 && FD_ISSET(FilterFdIn, &WriteQ)) {
	int WroteLen;

	if ((WroteLen = write(FilterFdIn, FilterQueue, FilterQueueLen)) < 0) {
	  char estr[256];
	  sprintf(estr, "Write to fd failed: %s\n", strerror(errno));
	  AddEStr(estr, 0, 0);
	  sleep(1);
	  Done(1);
	}

	FilterQueueLen -= WroteLen;
	memmove(FilterQueue, FilterQueue + WroteLen, FilterQueueLen);
      }
    }
  }
}

void Run() {
	MainProc();
}

void ProcessArgs(int argc, char *argv[]) {
  static int been_here;
  int ac, swp = 0;

  for (ac = 1; ac < argc; ac++) {
    switch (swp) {
      case 1:	
		if (strchr(argv[ac], '!')) {
		  char *s = strchr(argv[ac], '!');
		  *s = 0;
		  strncpy(ConnPassword, s + 1, 128);
		}
		if (strchr(argv[ac], ':')) {
		  char *s = strchr(argv[ac], ':');
		  *s = 0;
		  HostPort = atoi(s + 1);
		  *HostPortStr = 0;
		}
		strncpy(HostName, argv[ac], 256);
		break;
      case 2:	HostPort = atoi(argv[ac]);
		*HostPortStr = 0;
		break;
      case 3:	strncpy(ConnPassword, argv[ac], 128);
		strcpy(argv[ac], "*");
		break;
      case 4:	BurstLines = atoi(argv[ac]);
		*BurstLinesStr = 0;
		break;
    }

    if (swp) {
      swp = 0;
      continue;
    }

    if (! strcmp(argv[ac], "-h") || ! strcmp(argv[ac], "--help")) {
      printf("\nUsage:\t%s [-h|--help] [-c|--connect <host>[:<port>][!<password>]]\n", argv[0]);
      printf("\t[-p|--port <port>] [-w|--password <password>] [-l|--alone]\n");
      printf("\t[-m|--forcemono] [-b|--burst <lines>] [<profile>]\n\n");
      printf("Parameters override values from config file which override defaults.\n\n");
      printf("-h\tDisplay this help\n");
      printf("-c\tConnect to <host> (defaults to %s)\n", HostName);
      printf("-p\tConnect to <port> (defaults to %d)\n", HostPort);
      printf("-w\tUse <password> when connecting to ewrecv\n");
      printf("-l\tDon't attach to ewrecv (for testing, doesn't send exchange anything)\n");
      printf("-m\tForce mono mode\n");
      printf("-b\tBurst length; <lines> negative is full burst, zero is no burst.\n");
      printf("profile\tSpecific options file ~/.ewterm.options.<profile>\n\n");
      printf("Please report bugs to <pasky@ji.cz>.\n");
      exit(1);
    } else if (! strcmp(argv[ac], "-c") || ! strcmp(argv[ac], "--connect")) {
      swp = 1;
    } else if (! strcmp(argv[ac], "-p") || ! strcmp(argv[ac], "--port")) {
      swp = 2;
    } else if (! strcmp(argv[ac], "-w") || ! strcmp(argv[ac], "--password")) {
      swp = 3;
    } else if (! strcmp(argv[ac], "-l") || ! strcmp(argv[ac], "--alone")) {
      connection = (void *) 0x1; /* EHM */
    } else if (! strcmp(argv[ac], "-m") || ! strcmp(argv[ac], "--forcemono")) {
      ForceMono = '1';
    } else if (! strcmp(argv[ac], "-b") || ! strcmp(argv[ac], "--burst")) {
      swp = 4;
    } else if (argv[ac][0] == '-') {
      fprintf(stderr, "Unknown option \"%s\". Use -h or --help to get list of all the\n", argv[ac]);
      fprintf(stderr, "available options.\n");
      exit(1);
    } else if (!been_here) {
      if (*Profile) fprintf(stderr, "Warning: overriding previously specified profile %s.\n", Profile);
      printf("Using profile %s...\n", argv[ac]);
      strcpy(Profile, argv[ac]);
    }
  }

  been_here = 1;
}

int main(int argc, char **argv) {
	char *TmpPtr;
	char LogFName[256] = "";

	wl = wl;

	printf("EWTerm "VERSION" written by Petr Baudis, 2001, 2002\n");

	/* Set some directories to CWD */
	getcwd(LogFName, 255);
	TmpPtr = LogFName;
	while (*TmpPtr) TmpPtr++;
	*TmpPtr++ = '/';
	*TmpPtr = 0;
	strcpy(BufSaveFName, LogFName);
	strcpy(SendFileFName, LogFName);

	/* Read options */
	chdir(EWDIR);
	ReadOptions("ewterm.options");
	sprintf(MultiBuf, "%s/.ewterm.options", getenv("HOME"));
	ReadOptions(MultiBuf);

	/* Process args */
	ProcessArgs(argc, argv);
	if (*Profile) {
		snprintf(MultiBuf, 64, "%s/.ewterm.options.%s", getenv("HOME"), Profile);
		ReadOptions(MultiBuf);
		ProcessArgs(argc, argv); /* still possibly override with args */
	}

	if (*HostPortStr && *HostPortStr != '0') HostPort = atoi(HostPortStr);

	if (*BurstLinesStr) BurstLines = atoi(BurstLinesStr);

	/* Attach to ewrecv */
	if (connection != (void *)1) {
		AttachConnection();
	} else {
		connection = NULL;
	}

	/* Startup */
	Init();
	Run();
	Done(0);

	return 0;
}
