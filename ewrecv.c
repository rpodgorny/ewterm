/* This program acts as backend between ewterms and exchange. Note that this
 * program is supposed to run in trusted environment, as it's maybe possible
 * to convience it to change some connection attributes to client's merit
 * (ie. sending SEND packets to it, which are reserved to ewrecv->ewterm). */

#include <stdio.h>
#include <termios.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
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
#include <time.h>
#include <grp.h>

#include "version.h"

#include "logging.h"
#include "ascii.h"
#include "iproto.h"


/* Everything being written to logs should be terminated by \r\n, not just \n!
 */

/* TODO: All nonblocking stuff buffered! TCP support! */


struct connection *connection = NULL;
struct connection *to_destroy = NULL; /* Delayed destruction */

#define foreach_conn(but) \
if (connection) { \
	struct connection *cxx = connection; \
	do { \
		if (but != cxx) { \
			do { \
				struct connection *c = cxx;
#define foreach_conn_end \
			} while (0); \
		} \
		cxx = cxx->next; \
	} while (cxx != connection); \
}

#define	foreach_ipr_conn(but)	foreach_conn(but) if (c->IProtoState != IPR_HANDSHAKE) {
#define	foreach_ipr_conn_end	} foreach_conn_end

#define delete_from_list(c)	{ c->next->prev = c->prev; c->prev->next = c->next; }

/* Some types */

typedef unsigned char byte;
typedef unsigned long ulg;

/* Debug */

//#define	DEBUG
#ifdef DEBUG
FILE *debugf;
#define pdebug(fmt...) fprintf(debugf, fmt);
#else
#define pdebug(fmt...)
#endif

/* Talking with EWSD */

#define CUANSIZE 50
#define DEFDEVICE "/dev/tts/0"
#define DEFSPEED "9600"
#define PARAMS "7E1NNN"

char CuaName[CUANSIZE+1] = "";
char SpeedBuf[20] = "";
int CuaSpeed;

int CuaFd = -1;

enum {
	CM_READY, /* exchange is ready */
	CM_BUSY, /* exchange output in progress */
	CM_PBUSY, /* exchange prompt output in progress */
	CM_PROMPT, /* exchange prompt ready */
} CommandMode = 0;

int WantPrompt = 0; /* get prompt when CMD_READY */
char Prompt = 0; /* prompt type */
int LoggedIn = 0; /* logged in or not? */ /* 0 == not, 1 == in progress, 2 == yes */

#define WRITEBUF_MAX 16329
char WriteBuf[WRITEBUF_MAX] = ""; int WriteBufLen;

/* Sockets */

char SockName[256] = "";
unsigned int SockPort = 7880;
int SockFd = -1; /* listening socket */

int Reselect = 0;

/* Log files */

FILE *LogFl1 = NULL, *LogFl2 = NULL, *LogRaw = NULL;
char LogFName[256] = "", LogRawName[256] = "", DailyLogFNameTemplate[256] = "";

int DailyLog = 0;

int Silent = 0, Verbose = 0;

void ReopenLogFile(), UpdateDailyLogFName(struct tm *tm), CheckDailyLog();

#define	log_msg(fmt...) {\
	if (DailyLog) CheckDailyLog(); \
	if (LogFl1) { fprintf(LogFl1, fmt); fflush(LogFl1); }\
	if (LogFl2) { fprintf(LogFl2, fmt); fflush(LogFl2); }\
	if (LogRaw) { fprintf(LogRaw, fmt); fflush(LogRaw); }\
	if (!Silent && stderr != LogFl2) { fprintf(stderr, fmt); fflush(stderr); }\
}


/* History */

#ifndef HISTLEN
#define HISTLEN 100
#endif

static char Lines[HISTLEN][256];
int LastLine = 0; /* number of the last line in the round buffer */
int LineLen = 0; /* length of the last line */

/* Locking */

#ifdef LOCKDIR
char LockName[100] = "";
#endif

static char *wl = "4z";

/* Terminal thingies */

static struct termios tios_line = {
	IGNBRK | IGNPAR,
	0,
	CREAD | CLOCAL,
	ICANON,
	N_TTY,
	{ 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0 }
};

static struct termios tios_raw = {
	IGNBRK | IGNPAR,
	0,
	CREAD | CLOCAL,
	0,
	N_TTY,
	{ 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

struct config_record {
	struct config_record *next;
	char channel[64];
	char port[64];
	byte available;
	byte csize;
	byte parity;
	byte stops;
	ulg baud;
	byte linemode;
	byte shake;
	byte crnli, crnlo;
} ConfRec;





/**************************************\
 *
 * functions
 *
\**************************************/




void Done(int Err) {
	pdebug("Done() %d\n", Err);

#ifdef LOCKDIR
	if (LockName[0] != 0 && LockName[0] != 10) {
		remove(LockName);
		LockName[0] = 0;
	}
#endif /* LOCKDIR */

	if (CuaFd >= 0) {
		close(CuaFd);
		CuaFd = -1;
	}
	if (SockFd >= 0) {
		close(SockFd);
		SockFd = -1;
	}
	unlink(SockName);

	{
		char *time_s;
		time_t tv;

		tv = time(NULL); time_s = ctime(&tv);
		log_msg("--- ewrecv: end session (%d) at %s\n", Err, time_s);
	}

	if (LogFl1) fclose(LogFl1);
	if (LogFl2) fclose(LogFl2);
	if (LogRaw) fclose(LogRaw);

	exit(Err);
}




/*
 *
 * Configure serial port
 *
 */

int set_baud_rate(struct termios *t, int rate) {
	int i;

	switch (rate) {
		case 50: i=B50; break;
		case 75: i=B75; break;
		case 110: i=B110; break;
		case 134: i=B134; break;
		case 150: i=B150; break;
		case 200: i=B200; break;
		case 300: i=B300; break;
		case 600: i=B600; break;
		case 1200: i=B1200; break;
		case 1800: i=B1800; break;
		case 2400: i=B2400; break;
		case 4800: i=B4800; break;
		case 9600: i=B9600; break;
		case 19200: i=B19200; break;
		case 38400: i=B38400; break;
		case 57600: i=B57600; break;
		case 115200: i=B115200; break;
		case 230400: i=B230400; break;
/*		case 460800: i=B460800; break; */
		default: return -1;
	}
	t->c_cflag = (t->c_cflag & ~CBAUD) | i;
	return 0;
}

static void parse_io_mode(struct config_record *r, char *baud, char *flags) {
	wl = wl;
	sscanf(baud, "%ld", &r->baud);

	if (strlen(flags) != 6) return;

	if ((flags[0] == '7' || flags[0] == '8')
	&& (flags[1] == 'N' || flags[1] == 'E' || flags[1] == 'O')
	&& (flags[2] == '1' || flags[2] == '2')
	&& (flags[3] == 'H' || flags[3] == 'S' || flags[3] == 'N')
	&& (flags[4] == 'C' || flags[4] == 'N' || flags[4] == 'B')
	&& (flags[5] == 'C' || flags[5] == 'N' || flags[5] == 'B')) {
		r->csize = flags[0] - '0';
		r->parity = flags[1];
		r->stops = flags[2] - '0';
		r->shake = flags[3];
		r->crnli = flags[4];
		r->crnlo = flags[5];
		return;
	}
}

void init_port(int Handle, struct config_record *r) {
	struct termios *t;
	int Ret;

	t = r->linemode==1 ? &tios_line : &tios_raw;
	t->c_cflag |= (r->csize == 8) ? CS8 : CS7;
	if (r->parity != 'N') t->c_cflag |= PARENB;
	if (r->parity == 'O') t->c_cflag |= PARODD;
	if (r->stops == 2) t->c_cflag |= CSTOPB;
	if (r->shake == 'H') {
		t->c_cflag |= CRTSCTS;
	} else if (r->shake == 'S') {
		t->c_iflag = IXON;
		t->c_cc[VSTART] = 0x11;
		t->c_cc[VSTOP] = 0x13;
	}
	if (r->linemode == 1) {
		switch (r->crnli) {
			case 'N': t->c_iflag |= INLCR; break;
			case 'B': t->c_iflag |= ICRNL; break;
		}
	}

	if (set_baud_rate(t, r->baud)) fprintf(stderr, "Invalid baud rate\r\n");
	t->c_cflag |= HUPCL; /* Hangup on close */
	Ret = tcsetattr(Handle, TCSANOW, t);
	if (Ret) {
		perror("Cannot set termios - tcsetattr()");
		Done(6);
	}
}




void SigTermCaught() {
	/* Quit properly */
	Done(0);
}

void SigHupCaught() {
	/* Reopen log file */
	int OldSilent;

	OldSilent = Silent;
	Silent = 0;
  
	{
		char *time_s;
		time_t tv;

		tv = time(NULL); time_s = ctime(&tv);
		log_msg("--- ewrecv: caught ->HUP at %s\n", time_s);
	}
  
	if (LogFl1) {
		if (pclose(LogFl1) < 0) log_msg("--- ewrecv: Hey! Cannot pclose lpr!\n");

		LogFl1 = popen("/usr/bin/lpr", "w");
		if (!LogFl1) log_msg("--- ewrecv: Hey! Cannot popen lpr!\n");
	}

	ReopenLogFile();

	if (LogRaw) {
		fclose(LogRaw);

		LogRaw = fopen(LogRawName, "a");
		if (!LogRaw) log_msg("--- ewrecv: Hey! Cannot fopen %s!\n", LogRawName);
	}

	{
		char *time_s;
		time_t tv;

		tv = time(NULL); time_s = ctime(&tv);
		log_msg("--- ewrecv: caught HUP-> at %s\n", time_s);
	}

	Silent = OldSilent;

	signal(SIGHUP, SigHupCaught);
}



void ReopenLogFile() {
	pdebug("ReopenLogFile()\n");

	if (!LogFl2) return;

	fclose(LogFl2);
  
	LogFl2 = fopen(LogFName, "a");
	if (!LogFl2) log_msg("--- ewrecv: Hey! Cannot fopen %s!\n", LogFName);
}

void UpdateDailyLogFName(struct tm *tm) {
	if (!DailyLog) return;

	snprintf(LogFName, 256, "%s/%04d-%02d-%02d", DailyLogFNameTemplate, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
}

static int lday;

void CheckDailyLog() {
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);

	if (!DailyLog) return;

	if (tm->tm_mday == lday) return;
	lday = tm->tm_mday;
	UpdateDailyLogFName(tm);
	ReopenLogFile();
}



void ReOpenSerial() {
	char *TmpPtr, *FirstPtr;
	int HisPID = 0;
	FILE *Fl;

	pdebug("ReOpenSerial()\n");

	/* Close old cua file and unlock it */
	if (CuaFd >= 0) {
		close(CuaFd);
		CuaFd = -1;
	}
#ifdef LOCKDIR
	if ((LockName[0] != 0) && (LockName[0] != 10)) {
		remove(LockName);
		LockName[0] = 0;
	}
#endif

	/* Open new file */
	{
		/* Lock new */

#ifdef LOCKDIR

		/* Make lock name */
		TmpPtr = CuaName;
		FirstPtr = TmpPtr;
		while (*TmpPtr) {
			if (*(TmpPtr++) == '/') FirstPtr = TmpPtr;
		}

		sprintf(LockName, "%s/LCK..%s", LOCKDIR, FirstPtr);

		/* Check for lock */
		Fl = fopen(LockName, "r");
		if (Fl) {
			char Buf[256];
			char *Locker;

			/* Look who locked it */
			fgets(Buf, 256, Fl);
			/* fscanf(Fl, "%d %s", &HisPID, Locker); */
			fclose(Fl);
			Locker = Buf;
			while (*Locker == ' ' || *Locker == '\t') Locker++;
			HisPID = atoi(Locker);
			if (strchr(Locker, ' ')) {
				Locker = strchr(Locker, ' ') + 1;
			} else {
				Locker = "UNKNOWN";
			}
			fprintf(stderr, "Device already locked by '%s' (PID %d).\r\n", Locker, HisPID);

			/* Try if process still exists */
			if (kill(HisPID, 0) < 0) {
				if (errno == ESRCH) {
					fprintf(stderr, "That process does not exist. Trying to remove... ");
					fflush(stderr);
	
					if (remove(LockName) < 0) {
						fprintf(stderr, "failed.\r\nremove(LockFile): %s\r\n", strerror(errno));
						LockName[0] = 0;
						return;
					} else {
						fprintf(stderr, "success.\r\n");
					}
				} else {
					LockName[0] = 0;
					return;
				}
			} else {
				LockName[0] = 0;
				return;
			}
		}

		/* Create our lock */
		Fl = fopen(LockName, "w");
		if (Fl == 0 && LockName[0]) {
			fprintf(stderr, "Cannot open lock file: %s !\r\n", strerror(errno));
			LockName[0] = 0;
		} else {
			fprintf(Fl, "%05d %s %s\n", getpid(), "ewterm", getlogin());
			fclose(Fl);
		}

		if (!LockName[0]) Done(5);

#endif /* LOCKDIR */

		/* Open modem file itself */
		if (CuaName) CuaFd = open(CuaName, O_RDWR);
		if (CuaFd < 0) {

#ifdef LOCKDIR
			if ((LockName[0] != 0) && (LockName[0] != 10)) {
				remove(LockName);
				LockName[0] = 0;
			}
#endif /* LOCKDIR */

			fprintf(stderr, "Aieee... cannot open serial file!\r\n");
			exit(2);
		} else {
			fcntl(CuaFd, F_SETFL, O_NONBLOCK);
		}
	}
}




  /*
   * (see ewterm's exch.c for human-readable description)
   * The protocol looks (maybe) like:
   * 
   * (close = i'm sending, closes = he's sending)
   * 
   * Communication codes:
   * 
   * ^B		STX	start of text		not used
   * 
   * ^C		ETX	end of text		close[s] sent chunk of data
   * 
   * ^D		EOT	end of transmission	cancel actual command
   * 
   * ^E		ENQ	enquery			checks if someone is there
   * 
   * ^F		ACK	acknowledge		request prompt
   * 						request input from terminal
   * 
   * ^G		BEL	bell			reply to ETX, confirmation
   * 
   * ^U		NAK	not acknowledged	client's idle
   * 						(sending zeroes periodically
   * 						then, ended with ETX of course)
   * 						(just now we are doing this
   * 						in way when we get ENQ, we send
   * 						NAK, three zeroes and ETX, not
   * 						sure if it's 100% ok, but hopefully
   * 						should work ;-)
   *
   * 		DC	internal protocol	see iproto.c
   *
   */


void LogCh(char Chr) {
	pdebug("LogCh() %c/x%x\n", Chr, Chr);

	if (Chr >= 32 || Chr == 10) {
		if (DailyLog) CheckDailyLog();
		if (LogFl1) fputc(Chr, LogFl1);
		if (LogFl2) fputc(Chr, LogFl2);
		if (Verbose && stderr && stderr != LogFl2) fputc(Chr, stderr);
	}

	if (LineLen < 255) {
		Lines[LastLine][LineLen++] = Chr;
		Lines[LastLine][LineLen] = 0;
	}

	if (Chr == 10) {
		if (LogFl1) fflush(LogFl1);
		if (LogFl2) fflush(LogFl2);
		if (Verbose && stderr && stderr != LogFl2) fflush(stderr);

		/* next line in history */
		LastLine++;
		if (LastLine >= HISTLEN) LastLine = 0; /* cycle */
		Lines[LastLine][0] = 0;
		LineLen = 0;
	}
}

int SendChar(struct connection *c, char Chr) {
	pdebug("SendChar() %c/x%x\n", Chr, Chr);

	if (LoggedIn && c && c != connection) return -1;

	if (c && c->authenticated < 2) return -1;

	if (Chr > 32 || Chr == 10 || Chr == 8 || Chr == 13) LogChar(Chr);

	if (Chr == 13) Chr = 10;
  
	if (Chr == 10) Chr = ETX; /* end of text */
  
	pdebug("SendChar() %c/x%x \n", Chr, Chr);

	pdebug("%p(%d)\n", c, c?c->IProtoState:-1);
	if (c && c->IProtoState != IPR_DC4) {
		foreach_conn (c) {
			if (!c->authenticated) continue;
			Write(c, &Chr, 1);
		} foreach_conn_end;
	}

	if (CuaFd < 0) return 1;

	if (WriteBufLen >= WRITEBUF_MAX - 1) {
		log_msg("--- ewrecv: write [%x] error, write buffer full! (over %d)\n", Chr, WRITEBUF_MAX);
		return 0;
	}

	WriteBuf[WriteBufLen++] = Chr;

	return 1;
}

int LastMask = 0;

void ProcessExchangeChar(char Chr) {
	static int prompt = 0;

	pdebug("ProcessExchangeChar() Chr = %d\n", Chr);

	if (prompt && Chr != ENQ) {
		prompt = 0;
		foreach_ipr_conn (NULL) {
			if (!c->authenticated) continue;
			IProtoSEND(c, 0x40, NULL);
		} foreach_ipr_conn_end;
	}

	if (Chr == ETX) {
		pdebug("cm %d\n", CommandMode);
		if (CommandMode == CM_PBUSY) {
			CommandMode = CM_PROMPT;

			LastMask = 0;
			foreach_ipr_conn (NULL) {
				if (!c->authenticated) continue;
				IProtoSEND(c, 0x46, "0");
			} foreach_ipr_conn_end;

			if (!Prompt) {
				char *lastlinet = Lines[LastLine];
				int linelent = LineLen;
				while (*lastlinet == ' ') lastlinet++, linelent--;

				if (linelent == 1 && lastlinet[linelent - 1] == '<') {
					Prompt = '<';
					if (!LoggedIn || LoggedIn == 1) {
						foreach_ipr_conn (NULL) {
							if (!c->authenticated) continue;
							IProtoSEND(c, 0x43, NULL);
						} foreach_ipr_conn_end;
						if (!LoggedIn) {
							foreach_ipr_conn (connection) {
								if (!c->authenticated) continue;
								IProtoSEND(c, 0x04, "RO");
							} foreach_ipr_conn_end;
						}
						LoggedIn = 2;
					}
				} else {
					Prompt = 'I';
				}
			}

			{
				char s[256];

				if (Prompt == '<') {
#if 0
					fprintf(stderr, "lines (%d) [%d] ::%s::%s::%s::\n", LastLine, (LastLine - 1 + HISTLEN) % HISTLEN,
					Lines[(LastLine - 1 + HISTLEN) % HISTLEN],
					Lines[(LastLine + HISTLEN) % HISTLEN],
					Lines[LastLine]);
#endif
					snprintf(s, 256, "<%s", Lines[(LastLine - 1 + HISTLEN) % HISTLEN]);
				} else {
					snprintf(s, 256, "%c", Prompt);
				}
				Prompt = 0;

				foreach_ipr_conn (NULL) {
					if (!c->authenticated) continue;
					pdebug("%p->%s\n", c, s);
					IProtoSEND(c, 0x41, s);
				} foreach_ipr_conn_end;
			}
		} else {
			CommandMode = CM_READY;
			if (WantPrompt) {
				WantPrompt = 0;
				SendChar(NULL, ACK);
			}
		}
		SendChar(NULL, BEL);
	} else if (Chr == ENQ) {
		if (CommandMode == CM_PBUSY) {
			CommandMode = CM_BUSY;
			prompt = 0;
		}
		SendChar(NULL, NAK);
		SendChar(NULL, '0');
		SendChar(NULL, '0');
		SendChar(NULL, '0');
		SendChar(NULL, ETX);
	} else if (Chr == ACK) {
		CommandMode = CM_PBUSY;
		prompt = 1; /* schedule sending of prompt to ewterms */
	} else if (Chr == 10 || Chr >= 32) {
		if (Chr == 10) {
			static int NewLines, Header;
			static char ActExchange[10] = "";
			char *lastlinet = Lines[LastLine];

			while (*lastlinet == ' ') lastlinet++;
			pdebug("Got newline, analyzing ::%s::\n", lastlinet);

			/* Analyze this line */
			if (!strncmp(lastlinet, "END JOB ", 8)) {
				foreach_ipr_conn (NULL) {
					if (!c->authenticated) continue;
					IProtoSEND(c, 0x45, lastlinet + 8);
				} foreach_ipr_conn_end;
			} else if (!strncmp(lastlinet, "PLEASE ENTER USERID", 19)) {
				Prompt = 'U';
				if (!LoggedIn) {
					LoggedIn = 1;
					foreach_ipr_conn (connection) {
						if (!c->authenticated) continue;
						IProtoSEND(c, 0x04, "RO");
					} foreach_ipr_conn_end;
				}
			} else if (!strncmp(lastlinet, "PLEASE ENTER CURRENT PASSWORD", 29)) {
				Prompt = 'P';
			} else if (!strncmp(lastlinet, "PLEASE ENTER ", 13) && strstr(lastlinet, "PASSWORD")) {
				if (strstr(lastlinet, " FILE ")) {
					Prompt = 'F';
				} else {
					Prompt = 'p';
				}
			} else if (strstr(lastlinet, "MASKNO:")) {
				char *m = strstr(lastlinet, "MASKNO:") + 7;
				foreach_ipr_conn (NULL) {
					if (!c->authenticated) continue;
					IProtoSEND(c, 0x46, m);
				} foreach_ipr_conn_end;
				LastMask = atoi(m);
			}
			pdebug("P %c\n", Prompt);

			if (Header) {
				int ActJob; char ActOMT[6] = ""; char Usrname[10] = "";
				char *job = lastlinet;
				char *omt = job + 13;
				char *mask= omt + 20;

				Header = 0;

				/* 3882         OMT-01/PEBA           2977/00007 */
				/* job          omt  [/uname]        [????/maskno] */
				ActJob = atoi(job);
				if (strlen(job) > 13) {
					/* We already have it otherwise. */
					if (omt[0] && omt[0] != ' ') { strncpy(ActOMT, omt, 6); ActOMT[6] = 0; }
					if (omt[6] == '/') {
						strncpy(Usrname, omt + 7, 9);
						Usrname[9] = 0;
						if (strchr(Usrname, ' ')) *strchr(Usrname, ' ') = 0;
						if (strchr(Usrname, '\n')) *strchr(Usrname, '\n') = 0;
					}
					if (strlen(omt) > 20) {
						mask = strchr(mask, '/');
						if (mask) LastMask = atoi(++mask);
					} else {
						LastMask = 0;
					}
					foreach_ipr_conn (NULL) {
						char s[256];
						sprintf(s, "%d", LastMask); pdebug("LM->%d\n", LastMask);
						if (!c->authenticated) continue;
						IProtoSEND(c, 0x46, s);
					} foreach_ipr_conn_end;
				}
				{
					char s[256];
					snprintf(s, 256, "%d,%s,%s,%s", ActJob, ActOMT, Usrname, ActExchange);
					pdebug("%s <- ::%s::\n", s, lastlinet);
					foreach_ipr_conn (NULL) {
						if (!c->authenticated) continue;
						IProtoSEND(c, 0x47, s);
					} foreach_ipr_conn_end;
				}
			}

			if (!*lastlinet) {
				NewLines++;
			} else if (strncmp(lastlinet, "CONTINUATION TEXT", 17)) {
				if (NewLines >= 4) {
					Header = 1;
					if (strchr(lastlinet, '/')) {
						*strchr(lastlinet, '/') = 0;
						strncpy(ActExchange, lastlinet, 9);
						ActExchange[9] = 0;
						lastlinet[strlen(lastlinet)] = '/';
					}
				}
				NewLines = 0;
			}

			if (LastMask) {
				int LI = LoggedIn;

				if (LastMask == 12062) {
					/* NOT AUTHORIZED TO OPEN A SESSION. */
					foreach_ipr_conn (NULL) {
						if (!c->authenticated) continue;
						IProtoSEND(c, 0x42, NULL);
					} foreach_ipr_conn_end;
					LoggedIn = 0;
				} else if (LastMask == 12060) {
					/* LOCKED. */
					foreach_ipr_conn (NULL) {
						if (!c->authenticated) continue;
						IProtoSEND(c, 0x42, NULL);
					} foreach_ipr_conn_end;
					LoggedIn = 0;
				} else if (LastMask == 12048) {
					/* SESSION REJECTED, NEW PASSWORD INVALID. */
					foreach_ipr_conn (NULL) {
						if (!c->authenticated) continue;
						IProtoSEND(c, 0x42, NULL);
					} foreach_ipr_conn_end;
					LoggedIn = 0;
				} else if (LastMask == 12055) {
					/* CURRENT PASSWORD EXPIRED, PLEASE ENTER NEW PASSWORD. */
					Prompt = 'p';
				} else if (LastMask == 12059) {
					/* SESSION REJECTED, USERID PATR   IN USE. */
					foreach_ipr_conn (NULL) {
						if (!c->authenticated) continue;
						IProtoSEND(c, 0x42, NULL);
					} foreach_ipr_conn_end;
					LoggedIn = 0;
				} else if (LastMask == 10115 || LastMask == 6904) {
					/* INVALID PASSWORD */
					foreach_ipr_conn (NULL) {
						if (!c->authenticated) continue;
						IProtoSEND(c, 0x42, NULL);
					} foreach_ipr_conn_end;
					LoggedIn = 0;
				} else if (LastMask == 6299) {
					/* SESSION FOR PATR   CANCELLED! */
					foreach_ipr_conn (NULL) {
						if (!c->authenticated) continue;
						IProtoSEND(c, 0x44, NULL);
					} foreach_ipr_conn_end;
					LoggedIn = 0;
				} else if (LastMask == 10119) {
					/* SESSION CANCELLED BY TIMEOUT */
					foreach_ipr_conn (NULL) {
						if (!c->authenticated) continue;
						IProtoSEND(c, 0x44, NULL);
					} foreach_ipr_conn_end;
					LoggedIn = 0;
				} else if (LastMask == 10397) {
					/* SESSION CANCELLED FROM TERMINAL */
					foreach_ipr_conn (NULL) {
						if (!c->authenticated) continue;
						IProtoSEND(c, 0x44, NULL);
					} foreach_ipr_conn_end;
					LoggedIn = 0;
				} else if (LastMask == 7 && !strncmp(lastlinet, "ENDSESSION;", 11)) {
					/* Logout - hope it will work properly.. */
					foreach_ipr_conn (NULL) {
						if (!c->authenticated) continue;
						IProtoSEND(c, 0x44, NULL);
					} foreach_ipr_conn_end;
					LoggedIn = 0;
				}

				if (!LoggedIn && LI) {
					foreach_ipr_conn (connection) {
						if (c->authenticated < 2) continue;
						IProtoSEND(c, 0x04, "RW");
					} foreach_ipr_conn_end;
				}
			}
		}
		LogCh(Chr);
	}

	if (LogRaw) fputc(Chr, LogRaw);
}


void AnnounceUser(struct connection *conn, int opcode);

void DestroyConnection(struct connection *conn) {
	AnnounceUser(conn, 0x06);

	close(conn->Fd);
	if (conn == connection) {
		if (conn->next != conn) {
			connection = conn->next;
			if (LoggedIn && !(connection->authenticated < 2)) IProtoSEND(connection, 0x04, "RW");
		} else {
			connection = NULL;
		}
	}
	delete_from_list(conn);
	FreeConnection(conn);

	/* Force rebuild of the fd tables. */
	Reselect = 1;
}

void ErrorConnection(struct connection *conn) {
	char *time_s;
	time_t tv;

	tv = time(NULL); time_s = ctime(&tv);
	log_msg("--- ewrecv: attached client %d terminated at %s\n", conn->id, time_s);

	DestroyConnection(conn);
}


void SetMaster(struct connection *conn) {
	if (conn->authenticated < 2) return;

	/* *COUGH* ;-) --pasky */
	connection = conn;

	foreach_ipr_conn (connection) {
		if (!c->authenticated) continue;
		IProtoSEND(c, 0x04, "RO");
	} foreach_ipr_conn_end;

	if (LoggedIn) IProtoSEND(connection, 0x04, "RW");
}

char *ComposeUser(struct connection *conn, int self) {
	static char s[1024];
	snprintf(s, 1024, "%s@%s:%d", conn->user ? conn->user : "UNKNOWN", conn->host, conn->id);
	return s;
}

void AnnounceUser(struct connection *conn, int opcode) {
	char *s = ComposeUser(conn, 0);
	foreach_ipr_conn (conn) {
		if (!c->authenticated) continue;
		IProtoSEND(c, opcode, s);
	} foreach_ipr_conn_end;
}

void AuthFailed(struct connection *conn) {
	char msg[1024] = "RecvGuard@evrecv:-1=";

	strcat(msg, "Unauthorized connection attempt from host ");
	strcat(msg, conn->host);
	strcat(msg, "!\n");

	foreach_ipr_conn (conn) {
		IProtoSEND(c, 0x3, msg);
	} foreach_ipr_conn_end;

	log_msg("--- ewrecv: unauthorized connection by client %d\n\n", conn->id);

	IProtoSEND(conn, 0x8, NULL);
	IProtoSEND(conn, 0x6, ComposeUser(conn, 1));

	/* Otherwise, FreeConnection() fails as ie. IProtoUser is at different
	* position than suitable when handlers are called. */
	to_destroy = conn;
	Reselect = 1;
}

void GotUser(struct connection *conn, char *uname, char *d) {
	char s[1024];

#if 0
	/* The client may pop up a dialog asking for the password, so we should be not so strict. We won't send it anything anyway until we will get the correct auth token. */
	if (!conn->authenticated) {
		/* We got no response to the cram ASK, and we sent this right after that. We are not authenticated, no burst and so on, buddy! */
		AuthFailed(conn);
		return;
	}
#endif

	conn->user = strdup(uname);
	log_msg("--- ewrecv: attached client %d is %s\n\n", conn->id, conn->user);
	AnnounceUser(conn, 0x05);
	snprintf(s, 1024, "!%s@%s:%d", conn->user ? conn->user : "UNKNOWN", conn->host, conn->id);
	IProtoSEND(conn, 0x05, s);
}

void GotPrivMsg(struct connection *conn, char *tg, int id, char *host, char *msg, char *d) {
	char s[1024];

	snprintf(s, 1024, "%s@%s:%d=%s", conn->user?conn->user:"", conn->host, conn->id, msg);

	foreach_ipr_conn (NULL) {
		if (!c->authenticated) continue;
		if (id >= 0) {
			if (id == c->id) {
				IProtoSEND(c, 0x03, s);
				return;
			}
		} else if (tg && *tg) {
			if (c->user && !strcasecmp(tg, c->user)) {
				if (host && *host) {
					if (!strcasecmp(host, c->host)) IProtoSEND(c, 0x03, s);
				} else {
					IProtoSEND(c, 0x03, s);
				}
			}
		} else if (host && *host) {
			if (!strcasecmp(host, c->host)) IProtoSEND(c, 0x03, s);
		} else {
			IProtoSEND(c, 0x03, s);
		}
	} foreach_ipr_conn_end;
}


void TakeOverRequest(struct connection *conn, char *d) {
	if (conn && conn->authenticated < 2) return;
	if (conn != connection) SetMaster(conn);
}

void CancelPromptRequest(struct connection *conn, char *d) {
	pdebug("CancelPromptRequest()\n");

	if (conn != connection) return;
	if (conn && conn->authenticated < 2) return;
  	if (CommandMode == CM_PROMPT) {
		Prompt = 0;
		CommandMode = CM_READY;
		SendChar(NULL, EOT);
	}
}

void LoginPromptRequest(struct connection *conn, char *d) {
	pdebug("LoginPromptRequest()\n");

	if (LoggedIn) return;
	if (conn && conn->authenticated < 2) return;
	SetMaster(conn);

	SendChar(NULL, ETX);
	SendChar(NULL, BEL);
	SendChar(NULL, ACK);
	LoggedIn = 1;
	LastMask = 0;
}

void PromptRequest(struct connection *conn, char *d) {
	if (conn != connection || (conn && conn->authenticated < 2)) return;
	if (CommandMode == CM_READY) {
		SendChar(NULL, ACK);
	} else //if (CommandMode != CM_PROMPT && CommandMode != CM_PBUSY) /* This may make some problems, maybe? There may be a situation when we'll want next prompt before processing the first one, possibly. Let's see. --pasky */
		WantPrompt = 1;
}

void SendBurst(struct connection *conn, char *lines, char *d) {
	int ln, li = HISTLEN, lmax;

	if (conn && ! conn->authenticated) return;

	if (isdigit(*lines)) {
		lmax = atoi(lines);
	} else {
		lmax = HISTLEN;
	}

	WriteChar(conn, SO); /* start burst */

	for (ln = LastLine + 1; ln < HISTLEN; ln++, li--) {
		if (li <= lmax) Write(conn, Lines[ln], strlen(Lines[ln]));
	}

	for (ln = 0; ln <= LastLine; ln++, li--) {
		if (li <= lmax) Write(conn, Lines[ln], strlen(Lines[ln]));
	}

	WriteChar(conn, SI); /* end burst */
}

void SendIntro(struct connection *conn) {
	WriteChar(conn, SO); /* start burst */

	foreach_conn (conn) {
		if (!c->authenticated) continue;
		IProtoSEND(conn, 0x05, ComposeUser(c, 0));
	} foreach_conn_end;

	if (LoggedIn == 2) IProtoSEND(conn, 0x43, NULL);

	if ((conn != connection && LoggedIn) || conn->authenticated < 2) {
		IProtoSEND(conn, 0x04, "RO");
	} else {
		IProtoSEND(conn, 0x04, "RW");
	}

	WriteChar(conn, SI); /* end burst */
}

struct connection *TryAccept(int Fd) {
	struct connection *conn;
	int NewFd;
	static struct sockaddr_in remote;
	static int remlen;

	NewFd = accept(Fd, (struct sockaddr *)&remote, &remlen);
	if (NewFd < 0) {
		if (errno != EWOULDBLOCK && errno != EINTR) {
			perror("--- ewrecv: accept() failed");
			Done(4);
		}
		return NULL;
	}

	{
		static struct conn_handlers h = {
			/* 2.1a */
			SendChar,
			NULL,
			GotUser,
			NULL,
			NULL,

			/* 2.1b */
			NULL,
			NULL,
			NULL,

			/* 2.2a */
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,

			/* 2.3a */
			GotPrivMsg,

			/* 2.1a */
			NULL,
			NULL,

			/* 2.2a */
			PromptRequest,
			LoginPromptRequest,
			CancelPromptRequest,
			TakeOverRequest,

			/* 0.5pre3 */
			NULL,

			/* 0.5rc2 */
			SendBurst,

			/* 0.5pre3 */
			SendIntro /* AuthSuccess */,
			AuthFailed,
		};

		conn = MakeConnection(NewFd, &h);
		if (!conn) {
			fprintf(stderr, "Unable to create connection!\n");
			close(NewFd);
			return NULL;
		}

		{
			struct hostent *he = gethostbyaddr((char *)&remote.sin_addr, sizeof(remote.sin_addr), AF_INET);
			char *n = he&&he->h_name?he->h_name:inet_ntoa(remote.sin_addr);
			conn->host = strdup(n);
			endhostent();
		}

		if (connection) {
			{
				/* less memory, more time.. but it should still be fast enough as we don't expect more than 3-5 users per ewrecv.. */
				int lowest_spot = 0; /* lowest free place for us */
				for (;;) {
					int b = 1;
					foreach_conn (NULL) {
						if (lowest_spot == c->id) {
							lowest_spot++;
							b = 0;
						}
					} foreach_conn_end;
					if (b) break;
				}
				conn->id = lowest_spot;
			}
			connection->prev->next = conn;
			conn->prev = connection->prev;
			connection->prev = conn;
			conn->next = connection;
		} else {
			connection = conn;
			connection->next = conn;
			connection->prev = conn;
			conn->id = 0;
		}
	}

	WriteChar(conn, DC1);

	if (*ConnPassword) {
		char *S = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
		int i;
		conn->authstr = malloc(129);
		for (i = 0; i < 128; i++) conn->authstr[i] = S[random()%strlen(S)];
		conn->authstr[128] = 0;
		IProtoASK(conn, 0x02, conn->authstr);
	}

	IProtoASK(conn, 0x01, NULL);

	if (!*ConnPassword) {
		conn->authenticated = 2;
		/* SendBurst(conn); */ /* Clients request it now. */
	}

	return conn;
}

int DeploySocket(char *SockName, int SockPort) {
	struct sockaddr_in addr;
	int SockFd, opt;

	pdebug("DeploySocket()\n");

	SockFd = socket(PF_INET, SOCK_STREAM, 0);
	if (SockFd < 0) {
		perror("socket()");
		Done(2);
	}

	addr.sin_family = AF_INET;
	if (*SockName) {
		struct hostent *host = gethostbyname(SockName);
		if (!host) {
			perror("gethostbyname()");
			exit(6);
		}
		addr.sin_addr.s_addr = ((struct in_addr *) host->h_addr)->s_addr;
	} else {
		addr.sin_addr.s_addr = INADDR_ANY;
	}
	addr.sin_port = htons(SockPort);

	opt = 1;
	if (setsockopt (SockFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)) < 0) {
		perror("setsockopt()");
		Done(2);
	}

	if (bind(SockFd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind()");
		Done(2);
	}

	if (listen(SockFd, 16) < 0) {
		perror("listen()");
		Done(2);
	}

	if (fcntl(SockFd, F_SETFL, O_NONBLOCK) < 0) {
		perror("fcntl()");
		Done(2);
	}

	printf("Deployed socket: %s:%d\n", SockName, SockPort);

	return SockFd;
}



int main(int argc, char *argv[]) {
	int ac, swp = 0, ForkOut = 1, PrintLog = 0, OldPID;

	printf("EWReceiver "VERSION" written by Petr Baudis, 2001, 2002\n");

	/* process options */  
	strcpy(CuaName, DEFDEVICE);
	strcpy(SpeedBuf, DEFSPEED);
  
	for (ac = 1; ac < argc; ac++) {
		switch (swp) {
			case 1: strncpy(CuaName, argv[ac], CUANSIZE + 1); break;
			case 2: strncpy(SpeedBuf, argv[ac], 11); break;
			case 3: strncpy(LogFName, argv[ac], 256); break;
			case 4:
				strncpy(SockName, argv[ac], 256);
				if (strchr(SockName, ':')) {
					char *s = strchr(SockName, ':');
					*s = 0;
					SockPort = atoi(s + 1);
					break;
				}
				break;
			case 6: SockPort = atoi(argv[ac]); break;
			case 5: strncpy(LogRawName, argv[ac], 256); break;
			case 7:
				strncpy(ConnPassword, argv[ac], 128);
				{
					int i;
					for (i = 0; argv[ac][i]; i++) argv[ac][i] = '*';
				}
				break;
			case 8:
				strncpy(ROConnPassword, argv[ac], 128);
				{
					int i;
					for (i = 0; argv[ac][i]; i++) argv[ac][i] = '#';
				}
				break;
		}

		if (swp) {
			swp = 0;
			continue;
		}
    
		if (!strcmp(argv[ac], "-h") || !strcmp(argv[ac], "--help")) {
			printf("\nUsage:\t%s [-h|--help] [-c|--cuadev <cuadev>] [-s|--speed <speed>]\n", argv[0]);
			printf("\t[-f|--logfile <file>] [-L|--daylog] [-p|--printlog] [-r|--rawfile <file>]\n");
			printf("\t[-H|--host <host>[:<port>]] [-P|--port <port>] [-w|--password <pwd>]\n");
			printf("\t[-W|--ropassword <pwd>] [-g|--fg] [-S|--silent] [-v|--verbose]\n\n");
			printf("-h\tDisplay this help\n");
			printf("-c\tConnect to <cuadev> cua device (defaults to %s)\n", DEFDEVICE);
			printf("-s\tSet <speed> speed on cua device (defaults to %s)\n", DEFSPEED);
			printf("-f\tLog to file <file>\n");
			printf("-L\tTake -f parameter as directory name and log each day to separate\n");
			printf("\tfile, each one named <file>/YYYY-MM-DD\n");
			printf("-p\tLog to printer through lpr\n");
			printf("-r\tLog raw traffic to file <file> (useful only for debugging)\n");
			printf("-H\tBind listening socket to addr <host> (defaults to %s)\n", SockName);
			printf("-P\tBind listening socket to port <port> (defaults to %d)\n", SockPort);
			printf("-w\tPassword required for the clients to authenticate\n");
			printf("-W\tPassword required for the clients to authenticate for R/O access\n");
			printf("-g\tStay in foreground, don't fork() away\n");
			printf("-S\tDon't log to stderr (makes sense only with -g)\n");
			printf("\t(unless error occurs while logs reopening)\n");
			printf("-v\tLog even normal traffic to stderr (is meaningful only with -g)\n\n");
			printf("Please report bugs to <pasky@ji.cz>.\n");
			exit(1);
		}

		if (!strcmp(argv[ac], "-c") || !strcmp(argv[ac], "--cuadev")) {
			swp = 1;
			continue;
		}

		if (!strcmp(argv[ac], "-s") || !strcmp(argv[ac], "--speed")) {
			swp = 2;
			continue;
		}

		if (!strcmp(argv[ac], "-f") || !strcmp(argv[ac], "--logfile")) {
			swp = 3;
			continue;
		}

		if (!strcmp(argv[ac], "-L") || !strcmp(argv[ac], "--daylog")) {
			DailyLog = 1;
			continue;
		}

		if (!strcmp(argv[ac], "-p") || !strcmp(argv[ac], "--printlog")) {
			PrintLog = 1;
			continue;
		}

		if (!strcmp(argv[ac], "-r") || !strcmp(argv[ac], "--rawfile")) {
			swp = 5;
			continue;
		}

		if (!strcmp(argv[ac], "-H") || !strcmp(argv[ac], "--host")) {
			swp = 4;
			continue;
		}

		if (!strcmp(argv[ac], "-P") || !strcmp(argv[ac], "--port")) {
			swp = 6;
			continue;
		}

		if (!strcmp(argv[ac], "-w") || !strcmp(argv[ac], "--password")) {
			swp = 7;
			continue;
		}

		if (!strcmp(argv[ac], "-W") || !strcmp(argv[ac], "--ropassword")) {
			swp = 8;
			continue;
		}

		if (!strcmp(argv[ac], "-g") || !strcmp(argv[ac], "--fg")) {
			ForkOut = 0;
			continue;
		}

		if (!strcmp(argv[ac], "-S") || !strcmp(argv[ac], "--silent")) {
			Silent = 1;
			continue;
		}

		if (!strcmp(argv[ac], "-v") || !strcmp(argv[ac], "--verbose")) {
			Verbose = 1;
			continue;
		}
	}
 
	/* open log files */
	StartLog();

	if (LogFName[0]) {
		if (LogFName[0] != '/') {
			char LogFName2[256];

			/* make the path absolute, we will be chdir()ing later */
			strcpy(LogFName2, LogFName);
			if (!getcwd(LogFName, 256)) fprintf(stderr, "Warning! Cannot get cwd - logging may not work properly.\r\n");
			strcat(LogFName, "/");
			strcat(LogFName, LogFName2);
		}

		if (DailyLog) {
			time_t t = time(NULL);
			struct tm *tm = localtime(&t);

			strcpy(DailyLogFNameTemplate, LogFName);
			UpdateDailyLogFName(tm);
			lday = tm->tm_mday;
		}
		LogFl2 = fopen(LogFName, "a");
		if (!LogFl2) fprintf(stderr, "Warning! Cannot fopen %s!\r\n", LogFName);
	}

	if (LogRawName[0]) {
		if (LogRawName[0] != '/') {
			char LogRawName2[256];

			/* make the path absolute, we will be chdir()ing later */
			strcpy(LogRawName2, LogRawName);
			if (!getcwd(LogRawName, 256)) fprintf(stderr, "Warning! Cannot get cwd - logging may not work properly.\r\n");
			strcat(LogRawName, "/");
			strcat(LogRawName, LogRawName2);
		}

		LogRaw = fopen(LogRawName, "a");
		if (!LogRaw) fprintf(stderr, "Warning! Cannot fopen %s!\r\n", LogRawName);
	}

	if (PrintLog) {
		LogFl1 = popen("/usr/bin/lpr", "w");
		if (!LogFl1) fprintf(stderr, "Warning! Cannot popen lpr!\r\n");
	}

	/* install signal handlers */
	signal(SIGTERM, SigTermCaught);
	signal(SIGQUIT, SigTermCaught);
	signal(SIGINT, SigTermCaught);
	signal(SIGHUP, SigHupCaught);
	signal(SIGPIPE, SIG_IGN);

	/* open cua device */
	if (CuaName && *CuaName) {
		int Tmp;

		ReOpenSerial();

		while (!Tmp) {
			Tmp = atoi(SpeedBuf);

			switch (Tmp) {
				case 300:
				case 2400:
				case 9600:
				case 19200:
				case 38400:
				case 57600: CuaSpeed = Tmp; break;
				default:
					fprintf(stderr, "--- ewrecv: Invalid speed given! Switching to default.\r\n");
					Tmp = 0;
					strcpy(SpeedBuf, DEFSPEED);
			}
		}
    
		parse_io_mode(&ConfRec, SpeedBuf, PARAMS);
		init_port(CuaFd, &ConfRec);
	}

	/* deploy socket */
 	SockFd = DeploySocket(SockName, SockPort);

	/* release from current terminal, start new session etc */
	if (chdir("/") < 0) {
		perror("chdir(\"/\")");
		Done(3);
	}
    
	OldPID = getpid();
	if (ForkOut) {
		fclose(stdin);
		fclose(stdout);
		fclose(stderr);
    
		if (fork()) exit(0);
		setsid();
		if (fork()) exit(0);
    
		stderr = LogFl2; /* redirect stderr to log */
		if (!stderr) stderr = fopen("/dev/null", "a"); /* or to /dev/null */
	}

	/* update lock */
#ifdef LOCKDIR
	{
		FILE *Fl;
      
		Fl = fopen(LockName, "w");
		if (Fl) {
			fprintf(Fl, "%05d %s %s\n", getpid(), "ewterm", getlogin());
			fclose(Fl);
		}
	}
#endif
 
  /* delimiters */
#ifdef DEBUG
	debugf = stderr;
	pdebug("\n\n\n\n\n\n\n\n==================================\n\n\n\n\n\n\n\n");
#endif
	{
		char *time_s;
		time_t tv;
    
		tv = time(NULL); time_s = ctime(&tv);
		log_msg("--- ewrecv: new session pid %d -> %d at %s\n", OldPID, getpid(), time_s);
	}

	/* first attempt to accept() */
	TryAccept(SockFd);

#if 0
	/* make exchange happy, we are here and alive */
	SendChar(NULL, ETX);
	SendChar(NULL, BEL);
	SendChar(NULL, ACK);
	SendChar(NULL, EOT);
#endif

	/* select forever */
	for (;;) {
		int MaxFd;
		fd_set ReadQ;
		fd_set WriteQ;
		fd_set ErrorQ;

		/* prepare for select */
		Reselect = 0;

		if (to_destroy && !to_destroy->WriteBufferLen) {
			DestroyConnection(to_destroy);
			to_destroy = NULL;
		}

		MaxFd = 0;

		FD_ZERO(&ReadQ);
		FD_ZERO(&WriteQ);

		foreach_conn (NULL) { /* talking with terminal */
			FD_SET(c->Fd, &ReadQ);
			if (c->Fd > MaxFd) MaxFd = c->Fd;
			if (c->WriteBuffer) FD_SET(c->Fd, &WriteQ);
			FD_SET(c->Fd, &ErrorQ);
		} foreach_conn_end;

		if (SockFd >= 0) { /* listening on socket */
			FD_SET(SockFd, &ReadQ);
			if (SockFd > MaxFd) MaxFd = SockFd;
		}

		if (CuaFd >= 0) { /* talking with EWSD */
			FD_SET(CuaFd, &ReadQ);
			if (CuaFd > MaxFd) MaxFd = CuaFd;
			if (WriteBufLen > 0) FD_SET(CuaFd, &WriteQ);
		}

		/* select */
		if (select(MaxFd + 1, &ReadQ, &WriteQ, &ErrorQ, 0) < 0) {
			if (errno == EINTR) continue; /* try once more, just some silly signal */
			perror("--- ewrecv: Select failed");
			Done(4);
		} else {
			/* something from terminal */
			foreach_conn (NULL) {
				if (!FD_ISSET(c->Fd, &ReadQ)) continue;

				errno = 0;
				if (DoRead(c) <= 0 && errno != EINTR) {
					ErrorConnection(c);
				} else {
					char Chr;

					while (Read(c, &Chr, 1)) TestIProtoChar(c, Chr);
				}

				if (Reselect) goto reselect;
			} foreach_conn_end;

			/* something to terminal */
			foreach_conn (NULL) {
				if (!FD_ISSET(c->Fd, &WriteQ)) continue;

				errno = 0; /* XXX: Is write() returning 0 an error? */
				if (DoWrite(c) < 0 && errno != EINTR) ErrorConnection(c);

				if (Reselect) goto reselect;
			} foreach_conn_end;

			/* terminal error */
			foreach_conn (NULL) {
				if (!FD_ISSET(c->Fd, &ErrorQ)) continue;

				ErrorConnection(c);
				goto reselect;
			} foreach_conn_end;

			/* new connection from terminal */
			if (SockFd >= 0 && FD_ISSET(SockFd, &ReadQ)) {
				struct connection *c = TryAccept(SockFd);

				if (c) {
					char *time_s;
					time_t tv;

					tv = time(NULL); time_s = ctime(&tv);
					log_msg("--- ewrecv: attached client %d@%s connected at %s\n", c->id, c->host, time_s);
				} else {
					char *time_s;
					time_t tv;

					tv = time(NULL); time_s = ctime(&tv);
					log_msg("--- ewrecv: attached client ? failed to connect at %s\n", time_s);
				}
			}

			if (Reselect) goto reselect;

			/* something from cua */
			if (CuaFd >= 0 && FD_ISSET(CuaFd, &ReadQ)) {
				char Chr = 0;

				/* TODO: Use some buffer, we should get huge performance boost. --pasky */
				if (read(CuaFd, &Chr, 1) <= 0 && errno != EINTR) {
					perror("--- ewrecv: Read from CuaFd failed");
					Done(4);
				} else {
					if (CommandMode == CM_READY) CommandMode = CM_BUSY;

					ProcessExchangeChar(Chr);

					foreach_conn (NULL) {
						if (!c->authenticated) continue;
						Write(c, &Chr, 1);
					} foreach_conn_end;
				}
			}

			/* something to cua */
			if (CuaFd >= 0 && FD_ISSET(CuaFd, &WriteQ)) {
				int written;

				written = write(CuaFd, WriteBuf, WriteBufLen);
				if (written < 0) {
					if (errno == EINTR) {
						written = 0;
					} else {
						perror("--- ewrecv: Write to CuaFd failed");
						Done(4);
					}
				}

				/* shrink buffer */
				WriteBufLen -= written;
				memmove(WriteBuf, WriteBuf + written, WriteBufLen);
			}

reselect:
			;
		}
	}

	return 99; /* ehm? */
}
