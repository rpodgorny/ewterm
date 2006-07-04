#ifndef EW__IPROTO_H
#define EW__IPROTO_H

extern char ConnPassword[];
extern char ROConnPassword[];

enum IProtoState {
  IPR_HANDSHAKE, /* we sent DC1, but didn't get DC1 back yet */
  IPR_DC1, /* forwarding */
  IPR_DC2, /* loading SEND to IProtoPacket */
  IPR_DC3, /* loading ASK to IProtoPacket */
  IPR_DC4, /* forwarding only to exchange */
};

enum fwmode {
  FWD_INOUT,
  FWD_IN,
};

struct connection;

struct conn_handlers {
  int (*RecvChar)(struct connection *, char);

  /* Last (char *) is always "future data", everything following the part parsed by iproto.c. */

  void (*SENDVersion)(struct connection *, char *, char *);
  void (*SENDUser)(struct connection *, char *, char *);
  void (*SENDNotify)(struct connection *, char *);
  void (*SENDUnknownASK)(struct connection *, char *);

  void (*SENDForwardMode)(struct connection *, enum fwmode, char *);
  void (*SENDUserConnect)(struct connection *, int, char *, char *, char *, char *);
  void (*SENDUserDisconnect)(struct connection *, char *, char *, char *, char *);

  void (*SENDPromptStart)(struct connection *, char *);
  void (*SENDPromptEnd)(struct connection *, char, char *, char *);
  void (*SENDLoginError)(struct connection *, char *);
  void (*SENDLoginSuccess)(struct connection *, char *);
  void (*SENDLogout)(struct connection *, char *);
  void (*SENDJobEnd)(struct connection *, char *, char *);
  void (*SENDMaskNumber)(struct connection *, char *, char *);
  void (*SENDHeader)(struct connection *, char *, char *, char *, char *, char *);

  void (*SENDPrivMsg)(struct connection *, char *, int, char *, char *, char *);

  void (*ASKVersion)(struct connection *, char *);
  void (*ASKUser)(struct connection *, char *);

  void (*ASKPrompt)(struct connection *, char *);
  void (*ASKLoginPrompt)(struct connection *, char *);
  void (*ASKCancelPrompt)(struct connection *, char *);
  void (*ASKTakeOver)(struct connection *, char *);

  void (*ASKCRAM)(struct connection *, char *, char *);

  void (*ASKBurstMe)(struct connection *, char *, char *);

  void (*AuthSuccess)(struct connection *);
  void (*AuthFailed)(struct connection *);
};

struct connection {
  char *user;
  char *host;
  int id;
  int Fd;

  enum IProtoState IProtoState; /* InternalProtocolState */
  char *IProtoPacket;
  int IProtoPacketLen;

  struct conn_handlers *handlers;

  enum fwmode fwmode;

  int authenticated;
  char *authstr;

  char *ReadBuffer;
  int ReadBufferLen;
  char *WriteBuffer;
  int WriteBufferLen;

  struct connection *prev, *next;
};

struct connection *MakeConnection(int Fd, struct conn_handlers *);
void FreeConnection(struct connection *);

int Read(struct connection *, void *, int);
void Write(struct connection *, void *, int);
int DoRead(struct connection *);
int DoWrite(struct connection *);

void IProtoASK(struct connection *conn, char opcode, char *data);
void IProtoSEND(struct connection *conn, char opcode, char *data);
void TestIProtoChar(struct connection *conn, char Chr);

void MD5Sum(char *v, char *p);

#define WriteChar(c, chr) do {\
  char Ch = chr;\
  Write(c, &Ch, 1); \
} while(0)

#endif
