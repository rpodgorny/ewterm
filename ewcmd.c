#include <stdio.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <linux/fd.h>
#include <ctype.h>
#include <time.h>

#include "version.h"
#include "iproto.h"
#include "ascii.h"


#define COMMANDS_MAXLEN 102400

struct connection *connection = NULL;

int MainLoop = 0;

char Prompt = 0;

char HostName[256] = "localhost", HostPortStr[256] = "";
unsigned int HostPort = 7880;

char Exchanges[256] = "", Username[256] = "", Password[256] = "";
int login = 0, attach = 0, logout = 0, force_login = 0;
int login_timeout = 0, command_timeout = 0, timeout_denominator = 0;
time_t login_start = 0, command_start = 0;
int read_from_stdin = 1;
int verbose = 0;
int detaching = 0; // are we in the phase of detaching (expecting server to drop connection)?

char Commands[COMMANDS_MAXLEN+1] = "";

int logged_in = 0;

int jobs = 0; // number or currently running jobs
int want_quit = 0;

int Reconnect = 0;


int common_denominator(int a, int b) {
	if (a == 0) return b;
	if (b == 0) return a;

	while (b) {
		int t = b;
		b = a % b;
		a = t;
	}

	return a;
}

void Done(int Err) {
	switch (Err) {
		case 1: printf("EWCMD ERROR 1 (SELECT OR I/O ERROR)\n"); break;
		case 4: printf("EWCMD ERROR 4 (FAILED TO CREATE SOCKET)\n"); break;
		case 5: printf("EWCMD ERROR 5 (FAILED TO RESOLVE ADDRESS)\n"); break;
		case 6: printf("EWCMD ERROR 6 (CONNECTION FAILED)\n"); break;
		case 9: printf("EWCMD ERROR 9 (FAILED TO CREATE CONNECTION WRAPPER)\n"); break;
		case 101: printf("EWCMD ERROR 101 (LOGIN FAILED)\n"); break;
		case 102: printf("EWCMD ERROR 102 (ATTACH FAILED)\n"); break;
		case 103: printf("EWCMD ERROR 103 (LOGIN TIMEOUT)\n"); break;
		case 104: printf("EWCMD ERROR 104 (COMMAND TIMEOUT)\n"); break;
		case 105: printf("EWCMD ERROR 105 (LOGIN FAILED - SESSION IN USE)\n"); break;
	}

	exit(Err);
}

void DoneQuit() {
	Done(0);
}

void SigTermCaught() {
	if (logged_in) {
		// logout
		IProtoASK(connection, 0x46, NULL);
	} else {
		Done(0);
	}
}

void SigAlrmCaught() {
	signal(SIGALRM, SigAlrmCaught);
	alarm(1);
}

void Init() {
	signal(SIGTERM, SigTermCaught);
	signal(SIGQUIT, SigTermCaught);
	signal(SIGINT, SigTermCaught);
	signal(SIGALRM, SigAlrmCaught);
}

void SendChar(char c) {
	if (connection) Write(connection, &c, 1);
}

void SendUsername(char *s) {
	char *TmpPtr = s;

	while (*TmpPtr) SendChar(toupper(*TmpPtr++));
	SendChar(13);
	SendChar(10);

	login_start = time(NULL);
}

void SendPassword(char *s) {
	char *TmpPtr = s;

	SendChar(DC4);
	while (*TmpPtr) SendChar(*TmpPtr++);
	SendChar(DC1);
	SendChar(13);
	SendChar(10);
}

void SendNextCommand() {
	// we have no complete command in the queue
	if (!index(Commands, '\n')) return;

	if (!logged_in) return;

	if (Prompt == '<' && jobs > 0) return;

	if (Prompt != '<' && Prompt != 'I') {
		// get prompt if we don't have it
		IProtoASK(connection, 0x40, NULL);
		return;
	}

	char *TmpPtr = Commands;
	while (*TmpPtr != 0 && *TmpPtr != '\n') SendChar(toupper(*TmpPtr++));
	SendChar(13);
	SendChar(10);

	command_start = time(NULL);

	// move to next command
	memmove(Commands, TmpPtr+1, strlen(TmpPtr));

	if (Prompt == '<') jobs++;

	Prompt = 0;
}

void TryQuit() {
	if (!logged_in) {
		//printf("STILL LOGGED IN\n");
		return;
	}

	if (strlen(Commands)) {
		//printf("SOME COMMANDS STILL TO BE EXECUTED\n");
		return;
	}

	if (jobs > 0) {
		//printf("STILL HAVE SOME RUNNING JOBS\n");
		return;
	}

	if (read_from_stdin && !want_quit) {
		//printf("INPUT STILL NOT CLOSED\n");
		return;
	}

	if (logout) {
		IProtoASK(connection, 0x46, NULL);
	} else {
		// get the connection id first
		IProtoASK(connection, 0x54, NULL);
	}

	logged_in = 0;
}

void GotPromptStart(struct connection *c, char *d) {
}

void GotPromptEnd(struct connection *c, char type, char *job, char *d) {
	Prompt = type;

	switch (type) {
		case 'I':
		case '<': SendNextCommand(); break;
		case 'U': SendUsername(Username); break;
		case 'P': SendPassword(Password); break;
	}

	TryQuit();
}

void GotLoginError(struct connection *c, char *d) {
	Done(101);
}

void GotLoginSuccess(struct connection *c, char *d) {
	logged_in = 1;

	SendNextCommand();

	TryQuit();
}

void GotLogout(struct connection *c, char *d) {
	Done(0);
}

void GotJob(struct connection *c, char *job, char *d) {
	jobs--;

	SendNextCommand();

	TryQuit();
}

void GotConnectionId(struct connection *c, int id, char *d) {
	// print only on login
	if (login) printf("%d\n", id);

	// we have id, detach now...
	IProtoASK(c, 0x52, NULL);

	detaching = 1;
}

void GotAttach(struct connection *c, int status, char *d) {
	if (status == 0) Done(102);

	logged_in = 1;

	SendNextCommand();

	TryQuit();
}

char lastline[256] = "";
char curline[256] = "";

void CheckChr(struct connection *c, char Chr) {
	if (Chr < 32 && Chr != 9 && Chr != 10) return;

	strncat(curline, &Chr, 1);

	// newline has come
	if (Chr == 10) {
		// this is a fucking hack to handle unwanted interactivity (session in use)
		if (strstr(curline, "SESSION REJECTED") && strstr(curline, "IN USE")) {
			if (force_login) {
				SendChar('+');
				SendChar(13);
				SendChar(10);
			} else {
				Done(105);
			}
		}

		if (!verbose) {
			if (strstr(curline, "<") == curline
			|| strstr(curline, ":::") == curline
			|| strstr(curline, "SESSION REJECTED") == curline
			|| strstr(curline, "RE-OPEN SUCCESSFUL. NEW SESSION ESTABLISHED.") == curline
			|| (curline[0] == 10 && (lastline[0] == 10 || lastline[0] == 0))) {
				curline[0] = 0;
			}
		}

		fprintf(stdout, "%s", curline);
		fflush(stdout);

		if (curline[0]) {
			strcpy(lastline, curline);
			curline[0] = 0;
		}
	}
}

struct connection *MkConnection(int SockFd) {
	static struct conn_handlers h = {
		/* 2.1a */
		(int(*)(struct connection *, char))CheckChr,
		NULL,
		NULL,
		NULL,
		NULL,

		/* 2.1b */
		NULL,
		NULL,
		NULL,

		/* 2.2a */
		GotPromptStart,
		GotPromptEnd,
		GotLoginError,
		GotLoginSuccess,
		GotLogout,
		GotJob,
		NULL,
		NULL,

		/* 2.3a */
		NULL,

		/* 6.0 */
		NULL,
		NULL,

		/* 6.1 */
		NULL,

		// 6.2
		GotConnectionId,
		GotAttach,

		/* 2.1a */
		NULL,
		NULL, //GotUserRequest,

		/* 2.2a */
		NULL,
		NULL,
		NULL,
		NULL,

		/* 0.5pre3 */
		NULL,

		/* 0.5rc2 */
		NULL,

		/* 6.0 */
		NULL,
		NULL,
		NULL,

		/* 6.1 */
		NULL,
		NULL,

		// 6.2
		NULL,
		NULL,
		NULL,

		/* 0.5pre3 */
		NULL,
		NULL,
	};
	connection = MakeConnection(SockFd, &h);
	return connection;
}

void AttachConnection() {
	struct sockaddr_in addr;
	int SockFd;

	SockFd = socket(PF_INET, SOCK_STREAM, 0);
	if (SockFd < 0) Done(4);

	addr.sin_family = AF_INET;
	{
		struct hostent *host = gethostbyname(HostName);
		if (!host) {
			close(SockFd);
			Done(5);
		}
		addr.sin_addr.s_addr = ((struct in_addr *)host->h_addr)->s_addr;
	}
	addr.sin_port = htons(HostPort);

	if (connect(SockFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(SockFd);
		Done(6);
	}
	connection = MkConnection(SockFd);

	if (!connection) {
		close(SockFd);
		Done(9);
	}

	if (login) {
		IProtoASK(connection, 0x41, Exchanges);
	} else if (attach) {
		char tmp[20] = "";
		sprintf(tmp, "%d", attach);
		IProtoASK(connection, 0x53, tmp);
		connection->IProtoState = IPR_DC2; // HACK
	}
}

void MainProc() {
	for (;;) {
		int MaxFd;
		fd_set ReadQ;
		fd_set WriteQ;

		if (Reconnect) {
			close(connection->Fd);
			FreeConnection(connection);
			// To prevent floods. I know, this should be rather handled by ewrecv, but that would be nontrivial.
			sleep(2);
			AttachConnection();
			Reconnect = 0;
		}

		MaxFd = 0;

		FD_ZERO(&ReadQ);
		FD_ZERO(&WriteQ);

		// stdin
		if (read_from_stdin
		&& !want_quit
		&& strlen(Commands) < COMMANDS_MAXLEN) {
			FD_SET(0, &ReadQ);
		}

		if (connection) {
			FD_SET(connection->Fd, &ReadQ);
			if (connection->Fd > MaxFd) MaxFd = connection->Fd;
			if (connection->WriteBuffer) FD_SET(connection->Fd, &WriteQ);
		}

		struct timeval to;
		to.tv_sec = timeout_denominator;
		to.tv_usec = 0;
		struct timeval *top = &to;
		if (timeout_denominator == 0) top = NULL;

		int s = select(MaxFd + 1, &ReadQ, &WriteQ, 0, top);

		if (s < 0) {
			if (errno == EINTR) continue;
			Done(1);
		} else if (s == 0) {
			// timeout
		} else {
			// stdin
			if (read_from_stdin
			&& strlen(Commands) < COMMANDS_MAXLEN
			&& FD_ISSET(0, &ReadQ)) {
				char buf[256+1] = "";

				int to_read = COMMANDS_MAXLEN - strlen(Commands);
				if (to_read > 256) to_read = 256;

				int r = read(0, buf, to_read);
				buf[r] = 0;

				if (r == 0) {
					want_quit = 1;

					TryQuit();
				} else {
					strncat(Commands, buf, r);

					SendNextCommand();
				}
			}

			// Exchange input
			if (connection && FD_ISSET(connection->Fd, &ReadQ)) {
				errno = 0;
				if (DoRead(connection) <= 0) {
					if (detaching) {
						Done(0);
					} else {
						Done(1);
					}
				} else {
					int Chr;

					while (Read(connection, &Chr, 1)) {
						TestIProtoChar(connection, Chr);
					}
				}
			}

			// Exchange output
			if (connection && FD_ISSET(connection->Fd, &WriteQ)) {
				if (DoWrite(connection) < 0) Done(1);
			}
		}

		if (login_timeout && login_start && !logged_in) {
			if (time(NULL) - login_start > login_timeout) Done(103);
		}

		if (command_timeout && command_start && jobs) {
			if (time(NULL) - command_start > command_timeout) Done(104);
		}
	}
}

void ProcessArgs(int argc, char *argv[]) {
	// set default values
	if (getenv("EW_EXCHANGES")) strcpy(Exchanges, getenv("EW_EXCHANGES"));
	if (getenv("EW_USERNAME")) strcpy(Username, getenv("EW_USERNAME"));
	if (getenv("EW_PASSWORD")) strcpy(Password, getenv("EW_PASSWORD"));


	int ac, swp = 0;

	for (ac = 1; ac < argc; ac++) {
		switch (swp) {
			case 1:
				if (strchr(argv[ac], ':')) {
					char *s = strchr(argv[ac], ':');
					*s = 0;
					HostPort = atoi(s + 1);
					*HostPortStr = 0;
				}
				strncpy(HostName, argv[ac], 256);
				break;
			case 2:
				HostPort = atoi(argv[ac]);
				*HostPortStr = 0;
				break;
			case 3:
				strncpy(Exchanges, argv[ac], 256);
				break;
			case 4:
				strncpy(Username, argv[ac], 256);
				break;
			case 5:
				strncpy(Password, argv[ac], 256);
				break;
			case 6:
				login_timeout = atoi(argv[ac]);
				break;
			case 7:
				attach = atoi(argv[ac]);
				break;
			case 8:
				command_timeout = atoi(argv[ac]);
				break;
		}

		if (swp) {
			swp = 0;
			continue;
		}

		if (!strcmp(argv[ac], "-h") || !strcmp(argv[ac], "--help")) {
			printf("ewcmd "VERSION" written by Radek Podgorny, 2006, 2007\n\n");
			printf("Usage:\t%s [-h|--help] [-c|--connect <host>[:<port>] [command]\n", argv[0]);
			printf("\t[-p|--port <port>]\n");
			printf("\n");
			printf("-X exch1,exch2,...\n");
			printf("-U user\n");
			printf("-P pass\n");
			printf("[-l|--login]\n");
			printf("[-a|--attach] id\n");
			printf("[-o|--logout]\n");
			printf("[-L|--force-login]\tRe-open session when it's already in use\n");
			printf("[-v|--verbose]\n");
			printf("[-t|--login-timeout] sec\tExchange login timeout\n");
			printf("[-T|--command-timeout] sec\tCommand run timeout\n");
			printf("\n");
			printf("-h\tDisplay this help\n");
			printf("-c\tConnect to <host> (defaults to %s)\n", HostName);
			printf("-p\tConnect to <port> (defaults to %d)\n", HostPort);
			printf("-X\tExchanges to connect to\n");
			printf("-U\tUsername\n");
			printf("-P\tPassword\n");
			exit(1);
		} else if (! strcmp(argv[ac], "-c") || ! strcmp(argv[ac], "--connect")) {
			swp = 1;
		} else if (! strcmp(argv[ac], "-p") || ! strcmp(argv[ac], "--port")) {
			swp = 2;
		} else if (!strcmp(argv[ac], "-X")) {
			swp = 3;
		} else if (!strcmp(argv[ac], "-U")) {
			swp = 4;
		} else if (!strcmp(argv[ac], "-P")) {
			swp = 5;
		} else if (!strcmp(argv[ac], "-l") || !strcmp(argv[ac], "--login")) {
			login = 1;
		} else if (!strcmp(argv[ac], "-a") || !strcmp(argv[ac], "--attach")) {
			swp = 7;
		} else if (!strcmp(argv[ac], "-o") || !strcmp(argv[ac], "--logout")) {
			logout = 1;
		} else if (!strcmp(argv[ac], "-L") || !strcmp(argv[ac], "--force-login")) {
			force_login = 1;
		} else if (!strcmp(argv[ac], "-v") || !strcmp(argv[ac], "--verbose")) {
			verbose = 1;
		} else if (!strcmp(argv[ac], "-t") || !strcmp(argv[ac], "--login-timeout")) {
			swp = 6;
		} else if (!strcmp(argv[ac], "-T") || !strcmp(argv[ac], "--command-timeout")) {
			swp = 8;
		} else if (argv[ac][0] == '-') {
			fprintf(stderr, "Unknown option \"%s\". Use -h or --help to get list of all the\n", argv[ac]);
			fprintf(stderr, "available options.\n");
			exit(1);
		} else {
			strcat(Commands, argv[ac]);
			strcat(Commands, "\n");
		}
	}
}

int main(int argc, char **argv) {
	// Process args
	ProcessArgs(argc, argv);

	if (strlen(Commands)) read_from_stdin = 0;

	// if user does not specify a --login, --logout or --attach action, he wants a --login --logout combination
	if (!login && !logout && !attach) login = logout = 1;

	if (attach && !logout) {
	} else if (login && logout) {
	} else {
		// no commands allowed when only logging in or out
		Commands[0] = 0;
		read_from_stdin = 0;
	}

	if (login) {
		int exit = 0;

		if (!strlen(Exchanges)) {
			printf("EXCHANGES NOT SPECIFIED!\n");
			exit = 1;
		}
		if (!strlen(Username)) {
			printf("USERNAME NOT SPECIFIED!\n");
			exit = 1;
		}
		if (!strlen(Password)) {
			printf("PASSWORD NOT SPECIFIED!\n");
			exit = 1;
		}

		if (exit) Done(0);
	}

	timeout_denominator = common_denominator(login_timeout, command_timeout);

	if (*HostPortStr && *HostPortStr != '0') HostPort = atoi(HostPortStr);

	// Attach to ewrecv
	if (connection != (void *)1) {
		AttachConnection();
	} else {
		connection = NULL;
	}

	Init();
	MainProc();
	Done(0);

	return 0;
}
