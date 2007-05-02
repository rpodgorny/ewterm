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

#include "version.h"
#include "iproto.h"
#include "ascii.h"


struct connection *connection = NULL;

int MainLoop = 0;

char Prompt = 0;

char HostName[256] = "localhost", HostPortStr[256] = "";
unsigned int HostPort = 7880;

char Exchanges[256] = "", Username[256] = "", Password[256] = "";
int login = 0, attach = 0, logout = 0;

char Commands[1024] = "";

int logged_in = 0;

int jobs = 0; // number or currently running jobs
int want_quit = 0;

int Reconnect = 0;


void Done(int Err) {
	exit(Err);
}

void DoneQuit() {
	Done(0);
}

void SigIntCaught() {
printf("Got INT\n");
	// logout
	IProtoASK(connection, 0x46, NULL);

//	Done(0);
}

void SigTermCaught() {
printf("Got TERM\n");
	// logout
	IProtoASK(connection, 0x46, NULL);

//	Done(0);
//	signal(SIGTERM, SigTermCaught);
}

void SigAlrmCaught() {
	signal(SIGALRM, SigAlrmCaught);
	alarm(1);
}

void Init() {
	signal(SIGTERM, SigTermCaught);
	signal(SIGQUIT, SigTermCaught);
	signal(SIGINT, SigIntCaught);
	signal(SIGALRM, SigAlrmCaught);
}

void SendChar(char c) {
	if (connection) Write(connection, &c, 1);
}

void SendUsername(char *s) {
	char *TmpPtr = s;

	while (*TmpPtr) SendChar(*TmpPtr++);
	SendChar(13);
	SendChar(10);
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
printf("SENDING NEXT COMMAND\n");

	// we have no complete command in the queue
	if (!index(Commands, '\n')) return;

	if (!logged_in) return;

	if (Prompt != '<') {
		// get prompt if we don't have it
		IProtoASK(connection, 0x40, NULL);
	}

printf("REALLY SENDING NEXT COMMAND ->%s<-\n", Commands);

	char *TmpPtr = Commands;
	while (*TmpPtr != 0 && *TmpPtr != '\n') SendChar(*TmpPtr++);
	SendChar(13);
	SendChar(10);

	// move to next command
	memmove(Commands, TmpPtr+1, strlen(TmpPtr));

	jobs++;

	Prompt = 0;
}

void TryQuit() {
	if (!logged_in) {
		printf("STILL LOGGED IN\n");
		return;
	}

	if (jobs > 0) {
		printf("STILL HAVE SOME RUNNING JOBS\n");
		return;
	}

	if (!want_quit) {
		printf("INPUT STILL NOT CLOSED\n");
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
printf("TYPE: %c %d\n", type, type);

	switch (type) {
		case '<': SendNextCommand(); break;
		case 'U': SendUsername(Username); break;
		case 'P': SendPassword(Password); break;
	}

	TryQuit();
}

void GotLoginError(struct connection *c, char *d) {
	printf("LOGIN ERROR\n");
}

void GotLoginSuccess(struct connection *c, char *d) {
	logged_in = 1;

	SendNextCommand();
}

void GotLogout(struct connection *c, char *d) {
	exit(0);
}

void GotJob(struct connection *c, char *job, char *d) {
	jobs--;

	TryQuit();
}

void GotConnectionId(struct connection *c, int id, char *d) {
	printf("%d\n", id);

	// we have id, detach now...
	IProtoASK(c, 0x52, NULL);
}

void GotAttach(struct connection *c, int status, char *d) {
printf("Got attach\n");
	if (status == 0) {
		fprintf(stderr, "ATTACH FAILED!!!\n");
		exit(0);
	}

	logged_in = 1;

	SendNextCommand();
}

void CheckChr(struct connection *c, int Chr) {
	if (Chr < 32 && Chr != 9 && Chr != 10) {
	} else {
		fprintf(stdout, "%c", Chr);
		fflush(stdout);
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
	if (SockFd < 0) {
		perror("socket()");
		exit(6);
	}

	addr.sin_family = AF_INET;
	{
		struct hostent *host = gethostbyname(HostName);
		if (!host) {
			perror("gethostbyname()");
			close(SockFd);
			exit(6);
		}
		addr.sin_addr.s_addr = ((struct in_addr *)host->h_addr)->s_addr;
	}
	addr.sin_port = htons(HostPort);

	if (connect(SockFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("connect()");
		close(SockFd);
		exit(6);
	}
	connection = MkConnection(SockFd);

	if (!connection) {
		fprintf(stderr, "Unable to create connection!\n");
		close(SockFd);
		exit(9);
	}

	if (login) {
		IProtoASK(connection, 0x41, Exchanges);
	} else if (attach) {
		char tmp[20] = "";
		sprintf(tmp, "%d", attach);
		IProtoASK(connection, 0x53, tmp);
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
		if (!want_quit) FD_SET(0, &ReadQ);

		if (connection) {
			FD_SET(connection->Fd, &ReadQ);
			if (connection->Fd > MaxFd) MaxFd = connection->Fd;
			if (connection->WriteBuffer) FD_SET(connection->Fd, &WriteQ);
		}

		if (select(MaxFd + 1, &ReadQ, &WriteQ, 0, 0) < 0) {
			if (errno == EINTR) continue;
			perror("Select failed");
			Done(1);
		} else {
			// stdin
			if (FD_ISSET(0, &ReadQ)) {
				char buf[256] = "";
				int r = read(0, buf, 256);
				buf[r] = 0;

				if (r == 0) {
					want_quit = 1;

					TryQuit();
				} else {
printf("BUF: ->%s<-\n", buf);
					strcat(Commands, buf);

					SendNextCommand();
				}
			}

			/* Exchange input */
			if (connection && FD_ISSET(connection->Fd, &ReadQ)) {
				errno = 0;
				if (DoRead(connection) <= 0) {
					perror("Read from fd failed");
					Done(1);
				} else {
					int Chr;

					while (Read(connection, &Chr, 1)) {
						TestIProtoChar(connection, Chr);
					}
				}
			}

			// Exchange output
			if (connection && FD_ISSET(connection->Fd, &WriteQ)) {
				if (DoWrite(connection) < 0) {
					perror("Write to fd failed");
					Done(1);
				}
			}
		}
	}
}

void ProcessArgs(int argc, char *argv[]) {
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
			case 7:
				attach = atoi(argv[ac]);
				break;
		}

		if (swp) {
			swp = 0;
			continue;
		}

		if (!strcmp(argv[ac], "-h") || !strcmp(argv[ac], "--help")) {
			printf("ewcmd "VERSION" written by Radek Podgorny, 2006, 2007\n\n");
			printf("Usage:\t%s [-h|--help] [-c|--connect <host>[:<port>]\n", argv[0]);
			printf("\t[-p|--port <port>]\n");
			printf("\t-X exch1,exch2,...\n");
			printf("\t-U user\n");
			printf("\t-P pass\n");
			printf("\t--login\n");
			printf("\t--attach id\n");
			printf("\t--logout\n");
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
		} else if (!strcmp(argv[ac], "--login")) {
			login = 1;
		} else if (!strcmp(argv[ac], "--attach")) {
			swp = 7;
		} else if (!strcmp(argv[ac], "--logout")) {
			logout = 1;
		} else if (argv[ac][0] == '-') {
			fprintf(stderr, "Unknown option \"%s\". Use -h or --help to get list of all the\n", argv[ac]);
			fprintf(stderr, "available options.\n");
			exit(1);
		}
	}
}

int main(int argc, char **argv) {
	// Process args
	ProcessArgs(argc, argv);

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
