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


struct connection *connection = NULL;

int MainLoop = 0;

char HostName[256] = "localhost", HostPortStr[256] = "";
unsigned int HostPort = 7880;

int Reconnect = 0;


void Done(int Err) {
	exit(Err);
}

void DoneQuit() {
	Done(0);
}

void SigIntCaught() {
	Done(0);
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
	signal(SIGTERM, SigTermCaught);
	signal(SIGQUIT, SigTermCaught);
	signal(SIGINT, SigIntCaught);
	signal(SIGALRM, SigAlrmCaught);
}

void CheckChr(struct connection *c, int Chr) {
	if (Chr < 32 && Chr != 9 && Chr != 10) {
	} else {
		printf("%c", Chr);
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
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,

		/* 2.3a */
		NULL,

		/* 6.0 */
		NULL,
		NULL,

		/* 6.1 */
		NULL,

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

	// subsrcibe for alarms
	IProtoASK(connection, 0x44, NULL);
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
		}

		if (swp) {
			swp = 0;
			continue;
		}

		if (!strcmp(argv[ac], "-h") || !strcmp(argv[ac], "--help")) {
			printf("ewalarm "VERSION" written by Radek Podgorny, 2006, 2007\n\n");
			printf("Usage:\t%s [-h|--help] [-c|--connect <host>[:<port>]\n", argv[0]);
			printf("\t[-p|--port <port>]\n\n");
			printf("-h\tDisplay this help\n");
			printf("-c\tConnect to <host> (defaults to %s)\n", HostName);
			printf("-p\tConnect to <port> (defaults to %d)\n", HostPort);
			exit(1);
		} else if (! strcmp(argv[ac], "-c") || ! strcmp(argv[ac], "--connect")) {
			swp = 1;
		} else if (! strcmp(argv[ac], "-p") || ! strcmp(argv[ac], "--port")) {
			swp = 2;
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
