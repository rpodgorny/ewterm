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

#include <linux/sockios.h>
#include <linux/x25.h>

#include "version.h"

#include "logging.h"
#include "ascii.h"
#include "iproto.h"
#include "x25_packet.h"
#include "x25_utils.h"


/* Everything being written to logs should be terminated by \r\n, not just \n!
 */

/* TODO: All nonblocking stuff buffered! TCP support! */


struct connection *Conns[128];
int ConnCount;
/*
///struct connection *connection = NULL;
struct connection *to_destroy = NULL; // Delayed destruction

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

#define foreach_auth_conn(but)	foreach_conn(but) if (c->authenticated) {
#define foreach_auth_conn_end	} foreach_conn_end

#define delete_from_list(c)	{ c->next->prev = c->prev; c->prev->next = c->next; }
*/
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

///#define CUANSIZE 50
///#define DEFDEVICE "/dev/tts/0"
///#define DEFSPEED "9600"
///#define PARAMS "7E1NNN"

///char CuaName[CUANSIZE+1] = "";
///char SpeedBuf[20] = "";
///int CuaSpeed;

///int CuaFd = -1;

enum {
	CM_READY, /* exchange is ready */
	CM_BUSY, /* exchange output in progress */
	CM_PBUSY, /* exchange prompt output in progress */
	CM_PROMPT, /* exchange prompt ready */
} CommandMode = 0;

/// TODO: get rid of this shit
int LastId = 1;

#define WRITEBUF_MAX 16329

/* Sockets */

char SockName[256] = "";
unsigned int SockPort = 7880;
int SockFd = -1; /* listening socket */

int Reselect = 0;

/* x25 database */
struct X25Connection {
	char name[256];
	char address[256];
	int fd;
	int conn;
	time_t nexttry;
};

struct X25Connection X25Conns[32];
int X25ConnCount = 0;

char X25Local[256] = "";

/* Log files */

FILE *LogFl1 = NULL, *LogFl2 = NULL, *LogRaw = NULL, *LogTODO = NULL;
char LogFName[256] = "", LogRawName[256] = "", DailyLogFNameTemplate[256] = "", LogTODOFName[256] = "";

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

///static char *wl = "4z";

/* Terminal thingies */
/*
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
*/
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

void Done(int Err) {
	log_msg("Done() %d\n", Err);

#ifdef LOCKDIR
	if (LockName[0] != 0 && LockName[0] != 10) {
		remove(LockName);
		LockName[0] = 0;
	}
#endif /* LOCKDIR */

	if (SockFd >= 0) {
		close(SockFd);
		SockFd = -1;
	}
	unlink(SockName);

	int i = 0;
	for (i = 0; i < X25ConnCount; i++) {
		if (X25Conns[i].fd < 0) continue;

		close(X25Conns[i].fd);
		X25Conns[i].fd = -1;
	}

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

/// TODO: remove this shit
void LogoutRequest(struct connection *, char *);

void SigTermCaught() {
	/// TODO: this does not work since we quit before next select
	// Logout all connections
	int i = 0;
	for (i = 0; i < ConnCount; i++) {
		LogoutRequest(Conns[i], NULL);
	}

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

void Lock(FILE *Fl) {
	/* Create our lock */
	Fl = fopen(LockName, "w");
	if (Fl == 0 && LockName[0]) {
		fprintf(stderr, "Cannot open lock file: %s !\r\n", strerror(errno));
		LockName[0] = 0;
	} else {
		fprintf(Fl, "%05d %s %s\n", getpid(), "ewterm", getlogin());
		fclose(Fl);
	}
}

void Unlock() {
	if (LockName[0] != 0 && LockName[0] != 10) {
		remove(LockName);
		LockName[0] = 0;
	}
}

void GetLockInfo(FILE *Fl, int *pid, char *name) {
	/* Look who locked it */
	char Buf[256];
	fgets(Buf, 256, Fl);
	/* fscanf(Fl, "%d %s", &HisPID, Locker); */

	char *Locker = Buf;
	while (*Locker == ' ' || *Locker == '\t') Locker++;
	*pid = atoi(Locker);
	if (strchr(Locker, ' ')) {
		Locker = strchr(Locker, ' ') + 1;
	} else {
		Locker = "UNKNOWN";
	}
	strcpy(name, Locker);
}

void TryLock(char *name) {
	/* Check for lock */
	FILE *Fl = fopen(name, "r");
	if (Fl) {
		int HisPID = 0;
		char Locker[256];

		GetLockInfo(Fl, &HisPID, Locker);

		fclose(Fl);

		fprintf(stderr, "Device already locked by '%s' (PID %d).\r\n", Locker, HisPID);

		/* Try if process still exists */
		if (kill(HisPID, 0) < 0) {
			if (errno == ESRCH) {
				fprintf(stderr, "That process does not exist. Trying to remove... ");
				fflush(stderr);
	
				if (remove(LockName) < 0) {
					fprintf(stderr, "failed.\r\nremove(LockFile): %s\r\n", strerror(errno));
					name[0] = 0;
					return;
				} else {
					fprintf(stderr, "success.\r\n");
				}
			} else {
				name[0] = 0;
				return;
			}
		} else {
			name[0] = 0;
			return;
		}
	}

	Lock(Fl);
}

int OpenX25Socket(char *local, char *remote) {
	int ret = -1;

	struct sockaddr_x25 bind_addr, dest_addr;
	bzero(&bind_addr, sizeof(bind_addr));
	bzero(&dest_addr, sizeof(dest_addr));

	bind_addr.sx25_family = AF_X25;
	dest_addr.sx25_family = AF_X25;

	char x25local[256], x25remote[256];
	strcpy(x25local, local);
	strcpy(x25remote, remote);

	char *idx;
	if ((idx = index(x25local, '-')) != NULL) *idx = 0;
	if ((idx = index(x25remote, '-')) != NULL) *idx = 0;

	strcpy(bind_addr.sx25_addr.x25_addr, x25local);
	strcpy(dest_addr.sx25_addr.x25_addr, x25remote);

	ret = socket(AF_X25, SOCK_SEQPACKET, 0);
	if (ret < 0) {
		perror("socket");
		return -1;
	}

	int on = 1;
	setsockopt(ret, SOL_SOCKET, SO_DEBUG, &on, sizeof(on));

	unsigned long facil_mask = (
	X25_MASK_PACKET_SIZE
	| X25_MASK_WINDOW_SIZE
	| X25_MASK_CALLING_AE
	| X25_MASK_CALLED_AE);

	struct x25_subscrip_struct subscr;
	int extended = 0;
	subscr.global_facil_mask = facil_mask;
	subscr.extended = extended;
	strcpy(subscr.device, "x25tap0");

	int res = ioctl(ret, SIOCX25SSUBSCRIP, &subscr);
	if (res < 0) {
		perror("subscr");
		close(ret);
		return -1;
	}

	struct x25_facilities fac;
	fac.winsize_in = 7;
	fac.winsize_out = 7;
	fac.pacsize_in = 10;
	fac.pacsize_out = 10;
	fac.throughput = 0xdd;
	fac.reverse = 0x80;

	res = ioctl(ret, SIOCX25SFACILITIES, &fac);
	if (res < 0) {
		perror("fac");
		close(ret);
		return -1;
	}

	unsigned char calling_exten[10];
	unsigned char called_exten[10];

	to_bcd(calling_exten, local);
	to_bcd(called_exten, remote);

	struct x25_dte_facilities dtefac;
	dtefac.calling_len = 20;
	dtefac.called_len = 20;
	memcpy(&dtefac.calling_ae, &calling_exten, 10);
	memcpy(&dtefac.called_ae, &called_exten, 10);

	res = ioctl(ret, SIOCX25SDTEFACILITIES, &dtefac);
	if (res < 0) {
		perror("dtefac");
		close(ret);
		return -1;
	}

	res = bind(ret, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
	if (res < 0) {
		perror("bind");
		close(ret);
		return -1;
	}

/*	res = connect(ret, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
	if (res < 0) {
		perror("connect");
		close(ret);
		return -1;
	}
*/

	// set non-blocking mode
	long arg = fcntl(ret, F_GETFL, NULL);
	arg |= O_NONBLOCK; 
	fcntl(ret, F_SETFL, arg);

	// this will always fail but that's ok
	connect(ret, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

	return ret;
}

void ReOpenX25() {
	//log_msg("ReOpenX25()\n");

	///if (X25Fd >= 0) {
	///int i = 0;

	/// TODO
/*	for (i = 0; i < X25sCount; i++) {
		if (X25s[i].fd < 0) continue;

		shutdown(X25s[i].fd, SHUT_RDWR);
		close(X25s[i].fd);
		X25s[i].fd = -1;
	}
*/
#ifdef LOCKDIR
	Unlock();
#endif

	/* Open new file */
	{
		/* Lock new */
#ifdef LOCKDIR
		/* Make lock name */
		// TODO: sanitize this
		char *FirstPtr = rindex(X25Local, '/');
		sprintf(LockName, "%s/LCK..%s", LOCKDIR, FirstPtr);

		TryLock(LockName);

		if (!LockName[0]) Done(5);
#endif

		int i = 0;
		for (i = 0; i < X25ConnCount; i++) {
			if (X25Conns[i].fd >= 0 || X25Conns[i].conn == 1) continue;

			if (X25Conns[i].nexttry > time(NULL)) continue;

//printf("DEBUG: opening %s\n", X25Conns[i].address);

			X25Conns[i].fd = OpenX25Socket(X25Local, X25Conns[i].address);
			X25Conns[i].conn = 0;
			X25Conns[i].nexttry = time(NULL) + 30;
//printf("DEBUG: done opening %s\n", X25Conns[i].address);

#ifdef LOCKDIR
			///if (X25s[i].fd < 0) exit(2);
#endif
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
	///pdebug("LogCh() %c/x%x\n", Chr, Chr);

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

//TODO: this is slow and nasty
void LogStr(char *s, int len) {
	if (!LogTODO) {
		if (LogTODOFName[0]) {
			if (LogTODOFName[0] != '/') {
				char LogTODOFName2[256];

				/* make the path absolute, we will be chdir()ing later */
				strcpy(LogTODOFName2, LogTODOFName);
				if (!getcwd(LogTODOFName, 256)) fprintf(stderr, "Warning! Cannot get cwd - logging may not work properly.\r\n");
				strcat(LogTODOFName, "/");
				strcat(LogTODOFName, LogTODOFName2);
			}

			// it could be a fifo with no listener - we can't afford to block
			int fd = open(LogTODOFName, O_WRONLY | O_APPEND | O_NONBLOCK);
			if (fd < 0) {
				perror("LogTODO open");
			} else {
				LogTODO = fdopen(fd, "a");
				if (!LogTODO) fprintf(stderr, "Warning! Cannot fopen %s!\r\n", LogTODOFName);
			}
		}
	}

	if (!LogTODO) return;

	// TODO: this is ugly and slow, fix it
	int i = 0;
	for (i = 0; i < len; i++) fputc(s[i], LogTODO);

	fflush(LogTODO);
}

int SendChar(struct connection *c, char Chr) {
	if (c && c->authenticated < 2) return -1;

	if (Chr > 32 || Chr == 10 || Chr == 8 || Chr == 13) LogChar(Chr);

	if (Chr == 13) Chr = 10;
 
	pdebug("SendChar() %c/x%x \n", Chr, Chr);

	if (c->X25WriteBufLen >= WRITEBUF_MAX - 1) {
		log_msg("--- ewrecv: write [%x] error, write buffer full! (over %d)\n", Chr, WRITEBUF_MAX);
		return 0;
	}

	// filter newlines for X.25 connection but only when it's not the first char in buffer
	if (Chr != 10 || c->X25WriteBufLen == 0) {
		c->X25WriteBuf[c->X25WriteBufLen++] = Chr;
	}

	return 1;
}

int LastMask = 0;
// TODO: move this to somewhere else
void GenHeader(char *exch, char *apsver, char *patchver, char *date, char *time, unsigned short jobnr, char *omt, char *user, unsigned short msggrp, unsigned short mask, char *hint, char *out) {
	// TODO: better condition
	if (strlen(exch) == 0) {
		out[0] = 0;
		return;
	}

	char omtuser[32] = "";
	if (strlen(omt) && strlen(user)) sprintf(omtuser, "%s/%s", omt, user);

	char left_part[100] = "";

	sprintf(left_part, "%s/%s/%s", exch, apsver, patchver);
	sprintf(out, "%-53s %8s  %8s\n%04d %14s                %04d/%05d %26s\n\n", left_part, date, time, jobnr, omtuser, msggrp, mask, hint);
}

/* for X.25 communication */
void ProcessExchangePacket(int fd, struct packet *p) {
	pdebug("ProcessExchangePacket()\n");

	// TODO: a quick hack to filter empty packets. remove!
	if (block_getchild(p->data, "8-1") != NULL) return;

	struct block *b = NULL;
	unsigned short seq = 0;
	unsigned short jobnr = 0;
	char omt[200] = "";
	char user[200] = "";
	char exch[200] = "";
	char apsver[200] = "";
	char patchver[200] = "";
	char err[32000] = "";
	char unkx3_3[32000] = "";
	char hint[32000] = "";
	unsigned short msggrp = 0;
	unsigned char unkx5__0 = -1;
	unsigned char unkx5__1 = -1;
	unsigned char unkx5_4 = 0;
	unsigned short mask = 0;
	char prompt[32000] = "";
	char answer[32000] = "";
	char date[256] = "";
	char time[256] = "";

	b = block_getchild(p->data, "2");
	if (b && b->data) {
		seq = ntohs(*(unsigned short *)b->data);
printf("SEQ: %d\n", seq);
		jobnr = ntohs(*(unsigned short *)(b->data+2));
	}

	b = block_getchild(p->data, "3");
	if (b && b->data) {
		msggrp = ntohs(*(unsigned short *)b->data);
		mask = ntohs(*(unsigned short *)(b->data+2));
	}

	b = block_getchild(p->data, "3-3");
	if (b && b->data) strncpy(unkx3_3, (char *)b->data, b->len);

	b = block_getchild(p->data, "4-1");
	if (b && b->data) strncpy(exch, (char *)b->data, b->len);

	b = block_getchild(p->data, "4-2");
	if (b && b->data) strncpy(apsver, (char *)b->data, b->len);

	b = block_getchild(p->data, "4-3");
	if (b && b->data) strncpy(patchver, (char *)b->data, b->len);

	b = block_getchild(p->data, "4-4");
	if (b && b->data) strncpy(omt, (char *)b->data, b->len);

	b = block_getchild(p->data, "4-5");
	if (b && b->data) strncpy(user, (char *)b->data, b->len);

	b = block_getchild(p->data, "4-6");
	if (b && b->data) {
		sprintf(date, "%02d-%02d-%02d", *b->data, *(b->data+1), *(b->data+2));
	}

	b = block_getchild(p->data, "4-7");
	if (b && b->data) {
		sprintf(time, "%02d:%02d:%02d", *b->data, *(b->data+1), *(b->data+2));
	}

	b = block_getchild(p->data, "4-8");
	if (b && b->data) strncpy(hint, (char *)b->data, b->len);

	b = block_getchild(p->data, "5");
	if (b && b->data) {
		unkx5__0 = *b->data;
		unkx5__1 = *(b->data+1);
	}

	b = block_getchild(p->data, "5-2");
	if (b && b->data) strncpy(err, (char *)b->data, b->len);

	b = block_getchild(p->data, "5-3");
	if (b && b->data) strncpy(prompt, (char *)b->data, b->len);

	b = block_getchild(p->data, "5-4");
	if (b && b->data) {
		unkx5_4 = *b->data;
	}

	b = block_getchild(p->data, "7");
	if (b && b->data) strncpy(answer, (char *)b->data, b->len);


	int i = 0;
	for (i = 0; i < ConnCount; i++) {
		struct connection *c = Conns[i];

		if (p->sessid == 0 && c->alarms) {
			// it's alarm but we want it
		} else if (c->id == p->sessid) {
			// it's a message for us
		} else {
			// we don't want this message
			continue;
		}

		int gci = -1; // index of X.25 connection (global)
		int cci = -1; // index of X.25 connection (connection)

		int j = 0;
		for (j = 0; j < c->X25ConnCount; j++) {
			if (X25Conns[c->X25Conns[j]].fd == fd) {
				cci = j;
				gci = c->X25Conns[j];
				break;
			}
		}

		Write(c, "\n\n", 2);

		if (seq > 1) {
			char tmp[128] = "";
			sprintf(tmp, "CONTINUATION TEXT %04d\n\n", seq-1);
			Write(c, tmp, strlen(tmp));
		}

		char header[256] = "";
		GenHeader(exch, apsver, patchver, date, time, jobnr, omt, user, msggrp, mask, hint, header);
		if (strlen(header)) Write(c, header, strlen(header));

		// TODO: better condition
		if (jobnr) {
			char header[200] = "";
			sprintf(header, "%d,%s,%s,%s", jobnr, omt, user, exch);
printf("HEADER: %s\n", header);
			IProtoSEND(c, 0x47, header);
		}

		// TODO: better condition
		if (mask) {
			char mask_s[20] = "";
			sprintf(mask_s, "%d", mask);
			IProtoSEND(c, 0x46, mask_s);
printf("MASK: %d\n", mask);
		}

		if (strlen(err)) {
			Write(c, err, strlen(err));
		}
		if (strlen(answer)) {
			Write(c, answer, strlen(answer));
		}

		if (p->dir == 2) {
			/// TODO: make this condition more generic
			if (seq == 0x0701) {
				/// TODO: consolidate with the ones below
				c->X25LastConnId[cci] = p->connid;
				c->X25LastTail[cci] = p->tail;

				IProtoSEND(c, 0x40, NULL);
				Write(c, prompt, strlen(prompt));
				IProtoSEND(c, 0x41, "p");

				c->X25Prompt[cci] = 'p';
			} else if (strlen(prompt)) {
				// this is a command from EWSD
				c->X25LastConnId[cci] = p->connid;
				c->X25LastTail[cci] = p->tail;

				IProtoSEND(c, 0x40, NULL);
				Write(c, prompt, strlen(prompt));
				IProtoSEND(c, 0x41, "I");

				c->X25Prompt[cci] = 'I';
			} else {
				c->X25Prompt[cci] = 0;
			}
		} else if (p->dir == 3 && p->pltype == 1) {
			// "Command accepted" confirmation
			// TODO: send job start to ewterms?
			char tmp[256];
			sprintf(tmp, "%d\n\n", jobnr);
			Write(c, tmp, strlen(tmp));
		} else if (p->dir == 0x0c && p->pltype == 1) {
			if (strlen(unkx3_3)) {
				c->X25LoggedIn[cci] = 1;

				char msg[256] = "";
				sprintf(msg, "\n\n:::LOGIN SUCCESS ON %s\n\n", X25Conns[c->X25Conns[cci]].name);
				Write(c, msg, strlen(msg));

				// Login success to terms (only when logged to all exchanges)
				for (j = 0; j < c->X25ConnCount; j++) {
					if (c->X25LoggedIn[j] != 1) break;
				}

				// all exchanges have us logged in
				if (j == c->X25ConnCount) {
					IProtoSEND(c, 0x43, NULL);

					// TODO: is this really correct?
					// send input prompt (some command may be waiting in ewterm)
					IProtoSEND(c, 0x40, NULL);
					Write(c, "<", 1);
					IProtoSEND(c, 0x41, "<");
				}
			} else if (seq == 0x0303) {
				// INVALID PASSWORD
				IProtoSEND(c, 0x42, NULL);

				// logout from other exchanges, too...
				LogoutRequest(c, NULL);
			} else if (seq == 0x0307) {
				// SESSION IN USE
				char msg[256] = "";
				sprintf(msg, "\n\n:::SESSION ON %s IN USE, FORCE LOGIN? (+/-)\n\n", X25Conns[c->X25Conns[cci]].name);

				IProtoSEND(c, 0x40, NULL);
				Write(c, msg, strlen(msg));
				IProtoSEND(c, 0x41, "I");

				c->X25Prompt[cci] = 'R';
			} else {
				// other errors
				IProtoSEND(c, 0x42, NULL);

				// logout from other exchanges, too...
				LogoutRequest(c, NULL);
			}
		} else if (p->dir == 0x0e && p->pltype == 0) {
			// Session timeout (or maybe something else, too...)
			IProtoSEND(c, 0x44, NULL);
			c->X25LoggedIn[cci] = 0;

			// logout from other exchanges, too...
			LogoutRequest(c, NULL);
		}

		char jobnr_s[10] = "";
		sprintf(jobnr_s, "%04d", jobnr);

		// TODO: are these all cases?
		if (unkx5_4 == 2
		|| unkx5__0 == 2) {
			char tmp[256] = "";
			sprintf(tmp, "\nEND JOB %04d\n\n", jobnr);
			Write(c, tmp, strlen(tmp));

			IProtoSEND(c, 0x45, jobnr_s);
		} else if (unkx5__0 == 1) {
			char tmp[256] = "";
			sprintf(tmp, "\nEND TEXT JOB %04d\n\n", jobnr);
			Write(c, tmp, strlen(tmp));
		} else if (unkx5__0 == 0) {
			char tmp[256] = "";
			sprintf(tmp, "\nINTERRUPTION TEXT JOB %04d\n\n", jobnr);
			Write(c, tmp, strlen(tmp));
		}
	}
}

void AnnounceUser(struct connection *conn, int opcode);

void DestroyConnection(struct connection *conn) {
	AnnounceUser(conn, 0x06);

	close(conn->Fd);

	// find the index and move the connection from end there
	int i = 0;
	for (i = 0; i < ConnCount; i++) {
		if (Conns[i] == conn) break;
	}

	if (i < ConnCount-1) Conns[i] = Conns[ConnCount-1];

	ConnCount--;

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

/*
void SetMaster(struct connection *conn) {
	if (conn->authenticated < 2) return;

	// *COUGH* ;-) --pasky
	connection = conn;

	foreach_ipr_conn (connection) {
		if (!c->authenticated) continue;
		IProtoSEND(c, 0x04, "RO");
	} foreach_ipr_conn_end;

	if (LoggedIn) IProtoSEND(connection, 0x04, "RW");
}
*/
char *ComposeUser(struct connection *conn, int self) {
	static char s[1024];
	snprintf(s, 1024, "%s@%s:%d", conn->user ? conn->user : "UNKNOWN", conn->host, conn->id);
	return s;
}

void AnnounceUser(struct connection *conn, int opcode) {
	char *s = ComposeUser(conn, 0);
	if (!conn->authenticated) return;
	IProtoSEND(conn, opcode, s);
}

void AuthFailed(struct connection *conn) {
	char msg[1024] = "RecvGuard@evrecv:-1=";

	strcat(msg, "Unauthorized connection attempt from host ");
	strcat(msg, conn->host);
	strcat(msg, "!\n");

	IProtoSEND(conn, 0x3, msg);

	log_msg("--- ewrecv: unauthorized connection by client %d\n\n", conn->id);

	IProtoSEND(conn, 0x8, NULL);
	IProtoSEND(conn, 0x6, ComposeUser(conn, 1));

	/* Otherwise, FreeConnection() fails as ie. IProtoUser is at different
	* position than suitable when handlers are called. */
///	to_destroy = conn;
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

	if (!conn->authenticated) return;
	if (id >= 0) {
		if (id == conn->id) {
			IProtoSEND(conn, 0x03, s);
			return;
		}
	} else if (tg && *tg) {
		if (conn->user && !strcasecmp(tg, conn->user)) {
			if (host && *host) {
				if (!strcasecmp(host, conn->host)) IProtoSEND(conn, 0x03, s);
			} else {
				IProtoSEND(conn, 0x03, s);
			}
		}
	} else if (host && *host) {
		if (!strcasecmp(host, conn->host)) IProtoSEND(conn, 0x03, s);
	} else {
		IProtoSEND(conn, 0x03, s);
	}
}

void AlarmsOnRequest(struct connection *conn, char *d) {
	log_msg("AlarmsOnRequest()\n");

	if (conn && conn->authenticated < 2) return;

	conn->alarms = 1;

	IProtoSEND(conn, 0x48, NULL);
}

void AlarmsOffRequest(struct connection *conn, char *d) {
	log_msg("AlarmsOffRequest()\n");

	if (conn && conn->authenticated < 2) return;

	conn->alarms = 0;

	IProtoSEND(conn, 0x49, NULL);
}

void TakeOverRequest(struct connection *conn, char *d) {
	if (conn && conn->authenticated < 2) return;
}

void CancelPromptRequest(struct connection *conn, char *d) {
	log_msg("CancelPromptRequest()\n");

	int i = 0;
	for (i = 0; i < conn->X25ConnCount; i++) {
		conn->X25Prompt[i] = 0;
	}

	IProtoSEND(conn, 0x42, NULL);

/*
	if (conn && conn->authenticated < 2) return;
  	if (CommandMode == CM_PROMPT) {
		conn->Prompt = 0;
		CommandMode = CM_READY;
	}*/
}

void LoginPromptRequest(struct connection *conn, char *exch, char *d) {
	log_msg("LoginPromptRequest()\n");

	if (conn && conn->authenticated < 2) return;

	// logout first when needed
	LogoutRequest(conn, NULL);

	conn->X25ConnCount = 0;

	char *p;
	p = strtok(exch,",");
	while (p != NULL) {
		int found = 0;

		int i = 0;
		for (i = 0; i < X25ConnCount; i++) {
			if (strcasecmp(X25Conns[i].name, p) != 0) continue;
			
			conn->X25Conns[conn->X25ConnCount] = i;
			conn->X25ConnCount++;

			found = 1;
		}

		// not found
		if (found == 0) {
			char msg[256];
			sprintf(msg, "EXCHANGE %s NOT FOUND IN LIST!\n\n", p);
			Write(conn, msg, strlen(msg));
			IProtoSEND(conn, 0x42, NULL);
			return;
		}

		p = strtok(NULL, ",");
	}

	conn->X25User[0] = 0;
	conn->X25Passwd[0] = 0;

	IProtoSEND(conn, 0x41, "U");

	int i = 0;
	for (i = 0; i < conn->X25ConnCount; i++) {
		conn->X25Prompt[i] = 'U';
	}
}

void PromptRequest(struct connection *conn, char *d) {
	log_msg("PromptRequest()\n");

	if (conn && conn->authenticated < 2) return;
/*	if (CommandMode == CM_READY) {
		///SendChar(NULL, ACK);
	} else //if (CommandMode != CM_PROMPT && CommandMode != CM_PBUSY) // This may make some problems, maybe? There may be a situation when we'll want next prompt before processing the first one, possibly. Let's see. --pasky
		WantPrompt = 1;
*/

	if (conn->X25ConnCount == 0) return;

	int i = 0;
	for (i = 0; i < conn->X25ConnCount; i++) {
		if (conn->X25LoggedIn[i] == 0) return;
	}

	IProtoSEND(conn, 0x40, NULL);
	Write(conn, "<", 1);
	IProtoSEND(conn, 0x41, "<");
}

void SendBurst(struct connection *conn, char *lines, char *d) {
	int ln, li = HISTLEN, lmax;

	if (conn && !conn->authenticated) return;

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

	if (!conn->authenticated) return;
	IProtoSEND(conn, 0x05, ComposeUser(conn, 0));
/* TODO: what is this?
	if (conn->LoggedIn == 2) IProtoSEND(conn, 0x43, NULL);
*/
	if (conn->authenticated < 2) {
		IProtoSEND(conn, 0x04, "RO");
	} else {
		IProtoSEND(conn, 0x04, "RW");
	}

	WriteChar(conn, SI); /* end burst */
}

void LogoutRequest(struct connection *c, char *d) {
	if (!c->authenticated) return;

	int i = 0;
	for (i = 0; i < c->X25ConnCount; i++) {
		if (c->X25LoggedIn[i] == 0) continue;

		c->X25LoggedIn[i] = 0;
		c->X25Prompt[i] = 'X';

		char msg[256] = "";
		sprintf(msg, "\n\n:::LOGOUT FROM %s\n\n", X25Conns[c->X25Conns[i]].name);
		Write(c, msg, strlen(msg));

		IProtoSEND(c, 0x44, NULL);
	}
}

void ExchangeListRequest(struct connection *c, char *d) {
	if (!c->authenticated) return;

	char list[1024] = "";

	int i = 0;
	for (i = 0; i < X25ConnCount; i++) {
		if (i > 0) strcat(list, ",");
		strcat(list, X25Conns[i].name);
	}

	IProtoSEND(c, 0x50, list);
}


struct connection *TryAccept(int Fd) {
	printf("TryAccept()\n");

	struct connection *conn;
	int NewFd;
	static struct sockaddr_in remote;
	static socklen_t remlen;

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

			/* 6.0 */
			NULL,
			NULL,

			/* 6.1 */
			NULL,

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

			/* 6.0 */
			AlarmsOnRequest,
			AlarmsOffRequest,
			LogoutRequest,

			/* 6.1 */
			ExchangeListRequest,

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

		/// TODO: make this static or free it!
		conn->X25WriteBuf = malloc(WRITEBUF_MAX);
		conn->X25WriteBufLen = 0;

		Conns[ConnCount] = conn;
		ConnCount++;
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

	conn->id = LastId++;

	return conn;
}

int DeploySocket(char *SockName, int SockPort) {
	struct sockaddr_in addr;
	int SockFd, opt;

	///pdebug("DeploySocket()\n");

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

void InstallSignals() {
	/* install signal handlers */
	signal(SIGTERM, SigTermCaught);
	signal(SIGQUIT, SigTermCaught);
	signal(SIGINT, SigTermCaught);
	signal(SIGHUP, SigHupCaught);
	signal(SIGPIPE, SIG_IGN);
}

void StartLog2(int PrintLog) {
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
}

int main(int argc, char *argv[]) {
	int ac, swp = 0, ForkOut = 1, PrintLog = 0, OldPID;

	printf("EWReceiver "VERSION" written by Petr Baudis, 2001, 2002\n");
	printf("X.25 functionality added by Radek Podgorny, 2006, 2007\n");

	/* process options */  
	///strcpy(CuaName, DEFDEVICE);
	///strcpy(SpeedBuf, DEFSPEED);
  
	for (ac = 1; ac < argc; ac++) {
		switch (swp) {
///			case 1: strncpy(CuaName, argv[ac], CUANSIZE + 1); break;
///			case 2: strncpy(SpeedBuf, argv[ac], 11); break;
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
			case 9: strncpy(X25Local, argv[ac], 256); break;
			case 10: {
					char *p;
					p = strtok(argv[ac],":;,");
					while (p != NULL) {
						char *idx = index(p, '=');
						int len = idx - p;

						strncpy(X25Conns[X25ConnCount].name, p, len);
						strcpy(X25Conns[X25ConnCount].address, idx+1);
						X25Conns[X25ConnCount].fd = -1;

						X25ConnCount++;
						p = strtok(NULL, ":;,");
					}
				}
				break;
			case 11: strncpy(LogTODOFName, argv[ac], 256); break;
		}

		if (swp) {
			swp = 0;
			continue;
		}
    
		if (!strcmp(argv[ac], "-h") || !strcmp(argv[ac], "--help")) {
			printf("\nUsage:\t%s ", argv[0]);
			printf("[-h|--help] [-c|--cuadev <cuadev>] [-s|--speed <speed>]\n");
			printf("\t[-f|--logfile <file>] [-L|--daylog] [-p|--printlog] [-r|--rawfile <file>] [--todolog <file>]\n");
			printf("\t[-H|--host <host>[:<port>]] [-P|--port <port>] [-w|--password <pwd>]\n");
			printf("\t[-W|--ropassword <pwd>] [-g|--fg] [-S|--silent] [-v|--verbose]\n\n");
			printf("-h\tDisplay this help\n");
			printf("--x25local\tLocal endpoint X.25 address\n");
			printf("--x25remote\tRemote endpoint X.25 address\n");
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

/*		if (!strcmp(argv[ac], "-c") || !strcmp(argv[ac], "--cuadev")) {
			swp = 1;
			continue;
		}

		if (!strcmp(argv[ac], "-s") || !strcmp(argv[ac], "--speed")) {
			swp = 2;
			continue;
		}
*/
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

		if (!strcmp(argv[ac], "--x25local")) {
			swp = 9;
			continue;
		}

		if (!strcmp(argv[ac], "--x25remote")) {
			swp = 10;
			continue;
		}

		if (!strcmp(argv[ac], "--todolog")) {
			swp = 11;
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

	StartLog2(PrintLog);

	InstallSignals();

	if (X25Local && *X25Local) {
		/* open x25 device */
		ReOpenX25();
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
/* TODO
		if (to_destroy && !to_destroy->WriteBufferLen) {
			DestroyConnection(to_destroy);
			to_destroy = NULL;
		}
*/
		MaxFd = 0;

		FD_ZERO(&ReadQ);
		FD_ZERO(&WriteQ);

		int i = 0;

		// terminals
		for (i = 0; i < ConnCount; i++) {
			struct connection *c = Conns[i];

			FD_SET(c->Fd, &ReadQ);
			if (c->Fd > MaxFd) MaxFd = c->Fd;
			if (c->WriteBuffer) FD_SET(c->Fd, &WriteQ);
			FD_SET(c->Fd, &ErrorQ);
		}

		if (SockFd >= 0) { /* listening on socket */
			FD_SET(SockFd, &ReadQ);
			if (SockFd > MaxFd) MaxFd = SockFd;
		}

		/* talking with EWSD (X.25) */

		// check connection and reconnect when something is broken
		ReOpenX25();

printf("ADDING ");

		for (i = 0; i < X25ConnCount; i++) {
			int fd = X25Conns[i].fd;

			if (fd < 0) continue;

			// read queue
			if (X25Conns[i].conn == 1) {
printf("%s ", X25Conns[i].address);
				FD_SET(fd, &ReadQ);
				if (fd > MaxFd) MaxFd = fd;
			}

			// write queue
			if (X25Conns[i].conn == 0) {
				// wait for connect() result
				FD_SET(fd, &WriteQ);
				if (fd > MaxFd) MaxFd = fd;
printf("*%s ", X25Conns[i].address);
			} else {
				// connected, add only if we have something to say
				int add = 0;

				int j = 0;
				for (j = 0; j < ConnCount; j++) {
					if (Conns[j]->X25WriteBufLen > 0) add = 1;

					int k = 0;
					for (k = 0; k < Conns[j]->X25ConnCount; k++) {
						if (Conns[j]->X25Prompt[k] == 'X') add = 1;
					}
				}

				if (add) {
printf("**%s ", X25Conns[i].address);
					FD_SET(fd, &WriteQ);
					if (fd > MaxFd) MaxFd = fd;
				}
			}
		}
printf("\n");

		// timeout variables
		struct timeval tv;
		tv.tv_sec = 5;
		tv.tv_usec = 0;

		if (select(MaxFd + 1, &ReadQ, &WriteQ, &ErrorQ, &tv) < 0) {
			if (errno == EINTR) continue; /* try once more, just some silly signal */
			perror("--- ewrecv: Select failed");
			Done(4);
		} else {
printf("SELECT\n");
			/* something from terminal */
			int i = 0;
			for (i = 0; i < ConnCount; i++) {
				struct connection *c = Conns[i];

				if (!FD_ISSET(c->Fd, &ReadQ)) continue;
printf("FROM TERMINAL\n");

				errno = 0;
				if (DoRead(c) <= 0 && errno != EINTR) {
					ErrorConnection(c);
				} else {
					char Chr;
					while (Read(c, &Chr, 1)) TestIProtoChar(c, Chr);
				}

				if (Reselect) goto reselect;
			}

			/* something to terminal */
			for (i = 0; i < ConnCount; i++) {
				struct connection *c = Conns[i];

				if (!FD_ISSET(c->Fd, &WriteQ)) continue;

				errno = 0; /* XXX: Is write() returning 0 an error? */
				if (DoWrite(c) < 0 && errno != EINTR) ErrorConnection(c);

				if (Reselect) goto reselect;
			}

			/* terminal error */
			for (i = 0; i < ConnCount; i++) {
				struct connection *c = Conns[i];

				if (!FD_ISSET(c->Fd, &ErrorQ)) continue;

				ErrorConnection(c);
				goto reselect;
			}

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

			/* something from x25 */
			for (i = 0; i < X25ConnCount; i++) {
				int fd = X25Conns[i].fd;

				if (fd < 0 || !FD_ISSET(fd, &ReadQ)) continue;

				log_msg("FROM X.25\n");

				// find owner
				struct connection *c = NULL;
				int cci = -1;

				int j = 0;
				for (j = 0; j < ConnCount; j++) {
					int k = 0;
					for (k = 0; k < Conns[j]->X25ConnCount; k++) {
						if (X25Conns[Conns[j]->X25Conns[k]].fd == fd) {
							c = Conns[j];
							cci = k;
							break;
						}
					}

					// found
					if (c) break;
				}

				unsigned char buf[32000];

				int r = read(fd, buf, 32000);

				if (r <= 0 && errno != EINTR) {
					char msg[256] = "";
					sprintf(msg, "Read from %s failed", X25Conns[i].address);
					perror(msg);

					shutdown(fd, SHUT_RDWR);
					close(fd);
					X25Conns[i].fd = fd = -1;
					X25Conns[i].conn = 0;
				} else {
					// TODO: why else?
					///if (CommandMode == CM_READY) CommandMode = CM_BUSY;

					struct packet *p = packet_deserialize(buf, r);

					// send confirmation
					// TODO: create something like packet_copy()
					if (p && p->dir == 2 && p->pltype != 0) {
						struct packet *confirm = malloc(sizeof(struct packet));
						memcpy(confirm, p, sizeof(struct packet));
						confirm->data = NULL;
						confirm->rawdata = NULL;
						confirm->dir = 3;
						confirm->pltype = 6;

						unsigned char buf2[32000];
						int l = packet_serialize(confirm, buf2);
						write(fd, buf2, l);
						packet_delete(confirm);
					}

					if (p && p->rawdata && p->data == NULL) {
						// the block deserialize failed
						if (c->X25BufLen[cci] == 0) {
							// first truncated packet (copy with header)
							memcpy(c->X25Buf[cci], buf, r);
							c->X25BufLen[cci] = r;
							packet_delete(p); p = NULL;
						} else {
							// continuation (copy without header)
							memcpy(c->X25Buf[cci]+c->X25BufLen[cci], p->rawdata, p->rawdatalen);
							c->X25BufLen[cci] += p->rawdatalen;
							packet_delete(p); p = NULL;
						}
					}

					// test whether the packet is now complete
					if (!p && c->X25BufLen[cci] > 0) {
						p = packet_deserialize(c->X25Buf[cci], c->X25BufLen[cci]);
						if (!p || (p->rawdata && !p->data)) {
							// not complete yet
							packet_delete(p); p = NULL;
						} else {
							// complete
							c->X25BufLen[cci] = 0;
						}
					}
				
					if (p) {
						packet_print(p);

						ProcessExchangePacket(fd, p);

						packet_delete(p);
					}
				}

				log_msg("END FROM X.25\n");
			}

			// new (or renewed) X.25 connections
			for (i = 0; i < X25ConnCount; i++) {
				int fd = X25Conns[i].fd;

				if (fd < 0 || !FD_ISSET(fd, &WriteQ) || X25Conns[i].conn == 1) continue;

				int val;
				socklen_t len = sizeof(val);
				getsockopt(fd, SOL_SOCKET, SO_ERROR, &val, &len);
///printf("CONNECT %s -> %d\n", X25Conns[i].address, val);

				if (val == 0) {
					// success
printf("CONNECT %s -> %d\n", X25Conns[i].address, val);
					X25Conns[i].conn = 1;


					// set blocking mode
					long arg = fcntl(fd, F_GETFL, NULL);
					arg &= ~O_NONBLOCK; 
					fcntl(fd, F_SETFL, arg);
				} else {
					//failure
printf("CONNECT %s -> %d\n", X25Conns[i].address, val);
errno = val;
perror("CONN");
					close(X25Conns[i].fd);
					X25Conns[i].fd = -1;
				}

			}

			/* something to x25 */
			for (i = 0; i < ConnCount; i++) {
				struct connection *c = Conns[i];

				/// TODO: fix this workaround
				int sent = 0;

				int j = 0;
				for (j = 0; j < c->X25ConnCount; j++) {
					int fd = X25Conns[c->X25Conns[j]].fd;
					int cci = j;

					if (fd < 0 || !FD_ISSET(fd, &WriteQ)) continue;

					if (c->X25WriteBufLen == 0 && c->X25Prompt[cci] != 'X') continue;

					log_msg("TO X.25\n");
					sent = 1;

					struct packet *p = NULL;
					if (c->X25Prompt[cci] == 0) {
						// TODO: create function for this test
						// are all exchanges ready?
						int alllogged = 1;

						int k = 0;
						for (k = 0; k < c->X25ConnCount; k++) {
							if (c->X25LoggedIn[k] != 1) alllogged = 0;
						}

						if (alllogged) {
							p = command_packet(c->id, c->X25WriteBuf, c->X25WriteBufLen);
						}
					} else if (c->X25Prompt[cci] == 'I' || c->X25Prompt[cci] == 'p') {
						p = command_confirmation_packet(c->X25LastConnId[cci], c->id, c->X25LastTail[cci], c->X25WriteBuf, c->X25WriteBufLen);
						c->X25Prompt[cci] = 0;
					} else if (c->X25Prompt[cci] == 'U') {
						strncpy(c->X25User, c->X25WriteBuf, c->X25WriteBufLen);
						c->X25User[c->X25WriteBufLen] = 0;

						IProtoSEND(c, 0x41, "P");
						c->X25Prompt[cci] = 'P';
					} else if (c->X25Prompt[cci] == 'P') {
						strncpy(c->X25Passwd, c->X25WriteBuf, c->X25WriteBufLen);
						c->X25Passwd[c->X25WriteBufLen] = 0;

						// Username and password are messed with newlines, fix it
						char *idx = index(c->X25User, 10);
						if (idx) *idx = 0;
						idx = index(c->X25Passwd, 10);
						if (idx) *idx = 0;

						p = login_packet(c->id, c->X25User, c->X25Passwd, 0);
						c->X25Prompt[cci] = 0;
					} else if (c->X25Prompt[cci] == 'X') {
						p = logout_packet(c->id);
						c->X25Prompt[cci] = 0;
					} else if (c->X25Prompt[cci] == 'R') {
						if (c->X25WriteBufLen == 1 && c->X25WriteBuf[0] == '+') {
							p = login_packet(c->id, c->X25User, c->X25Passwd, 1);
						} else {
							char msg[256] = "";
							sprintf(msg, "\n\n:::WRONG ANSWER!!!\n\n");
							Write(c, msg, strlen(msg));

							LogoutRequest(c, NULL);
						}

						c->X25Prompt[cci] = 0;
					}

					unsigned char buf[32000];
					int len = packet_serialize(p, buf);
					packet_delete(p);

					/// TODO: loop until everything is sent
					int written = write(fd, buf, len);

					if (written < 0) {
						if (errno == EINTR) {
							written = 0;
						} else {
							perror("--- ewrecv: Write to X25Fd failed");
							Done(4);
						}
					}
				}

				if (sent) c->X25WriteBufLen = 0;
			}
reselect:
			;
		}
	}

	return 99; /* ehm? */
}
