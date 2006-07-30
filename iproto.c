#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ascii.h"
#include "iproto.h"
#include "md5.h"

char ConnPassword[128];
char ROConnPassword[128];

/* There's support for internal communication ewrecv<->ewterm here - DC1-DC4
 * control characters are used for that. The protocol is symmetric, unless
 * explicitly specified otherwise. We should ignore all unknown SEND packets
 * (each one has to have own DC2 switch) and reply with SEND 0x70 to all
 * unknown ASK packets, and we should maintain as big backwards compatibility
 * as possible.
 * 
 * Upon connect, both ewterm and ewrecv will send DC1. If the peer does NOT
 * send DC1, we MUST NOT send any other DC code anymore to the peer.
 *
 * There is an auth check by cram authorization done then - if it fails or the
 * peer does not support DC communication, the server/client MAY decide to
 * disconnect.
 *
 * After this handshake, ewrecv will query ewterm about user (ASK 0x01). Then,
 * ewterm usually asks for a burst and ewrecv feeds it then. The burst is
 * started by <SI> and terminated by <SO>, and the history of previous
 * communication is sent in the burst, and user connect packets are sent for
 * all users already connected. After receiving SEND 0x01.  user connect packet
 * for the client itself will be sent, prefixed with ! in <data>.
 *
 * Reality about compatibility: theoretically, it should be fine, but
 * practically there are some limitations, like killing off of unauthenticated
 * users and so on. */

/*
 * DC1:
 * 	Normal exchange protocol forwarding. Usually, all internal
 *	communication is always terminated with DC1, which switch the peer back
 *	to the forwarder mode. Note that ewrecv doesn't forward special chars
 *	from the exchange, but sends appropriate DC2 packets instead.
 *
 *
 * DC2:
 * 	We SEND something to the peer. The packet has format:
 * 	<DC2> <opcode> [<data>] <DCx>
 * 
 * 	Possible opcodes are:
 *
 * 		2.1a
 *
 * 	0xff	Version		<data> contains string describing the peer
 *      			version. For future compatibility, anything
 *      			following ',', ':' or '-' should be stripped.
 *      			Example: <DC2><0x00>2.1a
 * 	0x01	User		<data> contains string describing the user
 *      			running the thing. For future compatibility,
 *      			anything following '@' or ':' should be
 *      			stripped.
 *      			Example: <DC2><0x01>pasky
 * 	0x02	Notify		<data> contains message, which should be
 *      			broadcasted to all other ewterms, if the peer
 *      			is ewrecv, or shown (highlighted if possible)
 *      			to the user if the peer is ewterm. Newline is
 *      			automatically appended to the message when
 *      			printing it.
 *      			Example: <DC2><0x02>Hello, people!<DC1>
 *      0x80	Unknown ASK	We got ASK we can't understand. <data> contains
 *      			the ASK packet we can't understand.
 *      			Example: <DC2><0x80><0xab>blahblah?!<DC1>
 * 
 * 		2.1b
 *
 * 	These (0x40-0x47, 0x04-0x06) are valid only for ewrecv->ewterm direction.
 * 
 *      0x04	Forward mode	Forwarding mode change by <data> (strip
 *      			everything after [,.:]):
 *      				RW	Forwarding from/to ewterm
 *      					(default)
 *      				RO	Forwarding to ewterm only
 *      			Example: <DC2><0x03>RO<DC1>
 * 	0x05	User connect	Some user connected. <data> contains uname@host:id[,].*.
 * 				If <uname> starts with !, it's you.
 * 				Example: <DC2><0x05>!pasky@pasky.ji.cz:73<DC1>
 * 	0x06	User disconnect	Some user disconnected. <data> contains uname@host:id[,].*.
 * 				Example: <DC2><0x06>pasky@pasky.ji.cz:73<DC1>
 *
 * 		2.2a
 *
 * 	0x40	Prompt start	Prompt string starts NOW. <data> reserved.
 *      			Example: <DC2><0x40><DC1>*
 *      0x41	Prompt end	Prompt string ends NOW and we can send our
 *      			input. <data> has variable length, the first
 *      			byte can be:
 *      				'<'	Normal cmd prompt. Follows
 *      					number of the job, anything
 *      					after [,.:@;-] should be
 *      					stripped. If job number is 0,
 *      					the job number is unknown.
 *      				'I'	Input request prompt (anything
 *      					but '<', which is 0x00)
 *      				'U'	Username request
 *      				'P'	Login password request
 *      				'p'	Common password request
 *      				'F'	File password request
 *      			Example: *<DC2><0x41><1234<DC1>
 *      0x42	Login error	Login attempt was unsuccessful. <data> reserved.
 *      0x43	Login success	Login attempt was successful. <data> reserved.
 *      0x44	Logout correct	Correct logout happenned. <data> reserved.
 *      0x45	Job end		End of the job with number in <data>
 *      			(strip [,.:@;-].*). Note that this is sent for
 *      			each "END JOB", not only the active one.
 *      			Example: <DC2><0x45>1234<DC1>
 *      0x46	Mask number	The current message has mask number in <data>
 *      			(strip [,.:@;-].*).
 *      			Example: <DC2><0x46>5678<DC1>
 *      0x47	Header		Parsed data from header in <data>, separated
 *      			by ','. They are:
 *      			job,omt,username,exchange
 *      			More may follow in future.
 *      			Example: <DC2><0x47>1,OMT2,PEBA,GTS2<DC1>
 *
 *      	2.3a
 *
 *      This one is both-direction:
 *
 *      0x03	PrivMsg		This is "talk" message from one user to certain
 *      			(set of) user(s). The "user" part of <data> is
 *      			originator in ewrecv->ewterm direction and
 *      			target in ewterm->ewrecv direction. <data>
 *      			contains user@host:id=message, where anything
 *      			between id and '=' should be ignored.
 *      			Example: <DC2><0x03>pasky@pasky.ji.cz:0=Hello, world!<DC1>
 *
 * 		5.0pre3
 * 
 *      0x07	CRAM		This is reply to the CRAM ASK packet, containg
 *      			the MD5 sum. Strip [,.:@;-].*.
 * 
 *      0x08	CRAM failed	This indicates that the CRAM authentification
 *      			failed. This is usually sent right before
 *      			dropping the connection. Ignore the data part.
 * 
 * 
 * DC3:
 * 	We ASK the peer for something. The packet has format:
 * 	<DC3> <opcode> [<data>] <DCx>
 *
 * 	Possible opcodes are:
 *
 * 		2.1a
 *
 * 	0xff	Version		Show version, no <data> (leave blank).
 * 				Example: <DC3><0x00>
 *
 * 	0x01	User		Show user running the peer, no <data> (lb).
 * 				Example: <DC3><0x01>
 *
 * 		2.2a
 *
 * 	These (0x40-0x43) are valid only for ewterm->ewrecv direction.
 *
 * 	0x40	Prompt request	Request prompt from the exchange, no
 * 				<data> (lb).
 * 	0x41	Login request	Request login prompt from the exchange, no
 * 				<data> (lb).
 * 	0x42	Cancel prompt	Cancel current prompt, no data (lb).
 *
 * 		2.2b
 *
 * 	0x43	Takeover	Make me master, no data (lb).
 *
 * 		5.0pre3
 *
 * 	This one is valid only for ewrecv->ewterm direction.
 *
 * 	0x02	CRAM		MD5 the passed string + password and send back.
 * 				<data> contains the string, strip everything
 * 				after [,.:@;-].*.
 *
 * 		5.0rc2
 *
 * 	This is valid only for ewterm->ewrecv direction.
 *
 * 	0x3f	Burst me	Send ewterm the burst, containing the history
 * 				of previous activity. See above for the burst
 * 				format description. <data> contains number of
 * 				lines (everything if empty), strip everything
 * 				after [,.:@;-].*.
 *
 * DC4:
 *  	Like DC1, but we don't forward the stuff to other ewterms. Nice for
 *  	passwords stuff.
 */

#define BUF_GRAIN 256

void *
ShrinkBuffer(void *Buf, int Len)
{
  if (!(Len % BUF_GRAIN))
    Buf = realloc(Buf, Len + BUF_GRAIN);
  return Buf;
}


void StartHandshake(struct connection *conn) {
	WriteChar(conn, DC1);
}


struct connection *MakeConnection(int Fd, struct conn_handlers *handlers) {
	struct connection *conn = calloc(1, sizeof(struct connection));

	if (!conn) return NULL;
	if (Fd < 0) {
		fprintf(stderr, "Internal error: attempted to establish connection with negative Fd %d!\n", Fd);
		return NULL;
	}
	conn->Fd = Fd;
	conn->IProtoState = IPR_HANDSHAKE;
	conn->fwmode = FWD_INOUT;
	conn->handlers = handlers;
	StartHandshake(conn);

	return conn;
}

void FreeConnection(struct connection *conn) {
	if (conn->user) free(conn->user);
	if (conn->authstr) free(conn->authstr);
	if (conn->IProtoPacket) free(conn->IProtoPacket);
	if (conn->ReadBuffer) free(conn->ReadBuffer);
	if (conn->WriteBuffer) free(conn->WriteBuffer);
	free(conn);
}


int Read(struct connection *conn, void *data, int dlen) {
	int len = dlen > conn->ReadBufferLen ? conn->ReadBufferLen : dlen;

	if (!len) return 0;
	memcpy(data, conn->ReadBuffer, len);
	conn->ReadBufferLen -= len;
	memmove(conn->ReadBuffer, conn->ReadBuffer + len, conn->ReadBufferLen);
	return len;
}

int DoRead(struct connection *conn) {
	int len;

	conn->ReadBuffer = ShrinkBuffer(conn->ReadBuffer, conn->ReadBufferLen);
	len = BUF_GRAIN - (conn->ReadBufferLen % BUF_GRAIN);

	len = read(conn->Fd, conn->ReadBuffer + conn->ReadBufferLen, len);

	if (len <= 0) return len;
	conn->ReadBufferLen += len;
	return len;
}

void Write(struct connection *conn, void *data, int dlen) {
	int len;

	if (!dlen) return;

	conn->WriteBuffer = ShrinkBuffer(conn->WriteBuffer, conn->WriteBufferLen);
	len = BUF_GRAIN - (conn->WriteBufferLen % BUF_GRAIN);
	if (len > dlen) len = dlen;

	memcpy(conn->WriteBuffer + conn->WriteBufferLen, data, len);
	conn->WriteBufferLen += len;

	data += len;
	dlen -= len;
	if (dlen) Write(conn, data, dlen);
}

int DoWrite(struct connection *conn) {
	int len = 0;

	if (!conn->WriteBuffer) return 0;
	len = write(conn->Fd, conn->WriteBuffer, conn->WriteBufferLen);

	if (len <= 0) return len;
	conn->WriteBufferLen -= len;
	if (conn->WriteBufferLen) {
		memmove(conn->WriteBuffer, conn->WriteBuffer + len, conn->WriteBufferLen);
	} else {
		free(conn->WriteBuffer), conn->WriteBuffer = NULL;
	}
	return len;
}



void IProtoSEND(struct connection *conn, char opcode, char *data) {
	WriteChar(conn, DC2);
	WriteChar(conn, opcode);
	if (data) Write(conn, data, strlen(data));
	WriteChar(conn, DC1);
}

void IProtoASK(struct connection *conn, char opcode, char *data) {
	WriteChar(conn, DC3);
	WriteChar(conn, opcode);
	if (data) Write(conn, data, strlen(data));
	WriteChar(conn, DC1);
}

char *StripData(char *str, char *delim) {
	int idx = strcspn(str, delim);

	if (str[idx]) { str[idx] = 0; return str + idx + 1; } else return NULL; 
}

void MD5Sum(char *v, char *p) {
  struct md5_state_s ms;
  md5_byte_t digest[16]; int i;

  md5_init(&ms);
  md5_append(&ms, v, strlen(v));
  md5_append(&ms, ConnPassword, strlen(ConnPassword));
  md5_finish(&ms, digest);

  for (i = 0; i < 16; i++)
    snprintf(p + (i*2), 3, "%02x", digest[i]);
}

void
ProcessIProtoChar(struct connection *conn, unsigned char Chr) {
  switch (conn->IProtoState) {
    case IPR_HANDSHAKE:
      if (Chr != DC1) return; /* it's something *really* broken */
      break;

    case IPR_DC1:
      break;

    case IPR_DC2:
      if (!conn->IProtoPacket) break; /* nothing arrived */
      conn->IProtoPacket = realloc(conn->IProtoPacket,
	                           conn->IProtoPacketLen + 1);
      conn->IProtoPacket[conn->IProtoPacketLen] = '\0';
      conn->IProtoPacket++;

      if ((unsigned char) conn->IProtoPacket[-1] != 0x07);

      switch ((unsigned char) conn->IProtoPacket[-1]) {
	case 0xff: /* version */
	  if (conn->handlers->SENDVersion) {
	    conn->handlers->SENDVersion(conn, conn->IProtoPacket, StripData(conn->IProtoPacket, ":,-"));
	    break;
	  }
	  break;
	case 0x01: /* user */
	  if (conn->handlers->SENDUser) {
	    conn->handlers->SENDUser(conn, conn->IProtoPacket, StripData(conn->IProtoPacket, "@,"));
	    break;
	  }
	  break;
	case 0x02: /* notify */
	  if (conn->handlers->SENDNotify) {
	    conn->handlers->SENDNotify(conn, conn->IProtoPacket);
	    break;
	  }
	  break;
	case 0x80: /* unknown ASK */
	  if (conn->handlers->SENDUnknownASK) {
	    conn->handlers->SENDUnknownASK(conn, conn->IProtoPacket);
	    break;
	  }
	  break;
	case 0x04: /* forward mode */
	  {
	  char *d = StripData(conn->IProtoPacket, ":,.");
	  conn->fwmode = !strcmp(conn->IProtoPacket, "RO") ? FWD_IN : FWD_INOUT;
	  if (conn->handlers->SENDForwardMode) {
	    conn->handlers->SENDForwardMode(conn, conn->fwmode, d);
	    break;
	  }
	  }
	  break;
	case 0x05: /* user connect */
	  if (conn->handlers->SENDUserConnect) {
	    char *d = StripData(conn->IProtoPacket, ",");
	    char *y = strchr(conn->IProtoPacket, '@');
	    char *x;
	    char *u = conn->IProtoPacket;
	    int i = 0;
	    if (*u == '!') i = 1, u++;
	    if (y) *y = 0, y++;
	    x = strchr(y, ':');
	    if (x) *x = 0, x++;
	    conn->handlers->SENDUserConnect(conn, i, u, y, x, d);
	    break;
	  }
	  break;
	case 0x06: /* user disconnect */
	  if (conn->handlers->SENDUserDisconnect) {
	    char *d = StripData(conn->IProtoPacket, ",");
	    char *y = strchr(conn->IProtoPacket, '@');
	    char *x;
	    if (y) *y = 0, y++;
	    x = strchr(y, ':');
	    if (x) *x = 0, x++;
	    conn->handlers->SENDUserDisconnect(conn, conn->IProtoPacket, y, x, d);
	    break;
	  }
	  break;
	case 0x40: /* prompt start */
	  if (conn->handlers->SENDPromptStart) {
	    conn->handlers->SENDPromptStart(conn, conn->IProtoPacket);
	    break;
	  }
	  break;
	case 0x41: /* prompt end */
	  if (conn->handlers->SENDPromptEnd) {
	    char *d = StripData(conn->IProtoPacket, ",.:;@-");
	    conn->handlers->SENDPromptEnd(conn, conn->IProtoPacket[0], conn->IProtoPacket + 1, d);
	    break;
	  }
	  break;
	case 0x42: /* login error */
	  if (conn->handlers->SENDLoginError) {
	    conn->handlers->SENDLoginError(conn, conn->IProtoPacket);
	    break;
	  }
	  break;
	case 0x43: /* login success */
	  if (conn->handlers->SENDLoginSuccess) {
	    conn->handlers->SENDLoginSuccess(conn, conn->IProtoPacket);
	    break;
	  }
	  break;
	case 0x44: /* logout */
	  if (conn->handlers->SENDLogout) {
	    conn->handlers->SENDLogout(conn, conn->IProtoPacket);
	    break;
	  }
	  break;
	case 0x45: /* job end */
	  if (conn->handlers->SENDJobEnd) {
	    char *d = StripData(conn->IProtoPacket, ",.:;@-");
	    conn->handlers->SENDJobEnd(conn, conn->IProtoPacket, d);
	    break;
	  }
	  break;
	case 0x46: /* mask number */
	  if (conn->handlers->SENDMaskNumber) {
	    char *d = StripData(conn->IProtoPacket, ",.:;@-");
	    conn->handlers->SENDMaskNumber(conn, conn->IProtoPacket, d);
	    break;
	  }
	  break;
	case 0x47: /* header */
	  if (conn->handlers->SENDHeader) {
	    char *job = NULL;
	    char *omt = NULL;
	    char *username = NULL;
	    char *exchange = NULL;
	    char *d = NULL;
	    job = conn->IProtoPacket;
	    omt = strchr(job, ',');
	    if (omt) {
	      *omt = 0; omt++;
	      username = strchr(omt, ',');
	      if (username) {
		*username = 0; username++;
		exchange = strchr(username, ',');
		if (exchange) {
		  *exchange = 0; exchange++;
		  d = strchr(exchange, ',');
		  if (d) {
		    *d = 0; d++;
		  }
		}
	      }
	    }
	    conn->handlers->SENDHeader(conn, job, omt, username, exchange, d);
	    break;
	  }
	  break;
	case 0x03: /* privmsg */
	  if (conn->handlers->SENDPrivMsg) {
	    char *user = conn->IProtoPacket;
	    char *host = NULL, *id = NULL, *msg = NULL, *d = NULL;
	    int idn;

	    host = strchr(user, '@');
	    if (host) {
	      *host = 0; host++;
	      id = strchr(host, ':');
	      if (id) {
		*id = 0; id++;
		idn = strtol(id, &d, 10);
		msg = strchr(d, '=');
		if (msg) {
		  *msg = 0; msg++;
		}
	      }
	    }
	    conn->handlers->SENDPrivMsg(conn, user, idn, host, msg, d);
	    break;
	  }
	  break;
	case 0x07: /* cram */
	  {
	  char p[129];

	  StripData(conn->IProtoPacket, ",.:;@-");

	  if (!conn->authstr) {
	    /* protocol violation, assume the worst */
	    if (conn->handlers->AuthFailed) conn->handlers->AuthFailed(conn);
	    break;
	  }
	  MD5Sum(conn->authstr, p);
	  if (strcmp(p, conn->IProtoPacket)) {
	    if (*ROConnPassword) {
	      /* XXX */
	      char B[256];

	      strcpy(B, ConnPassword);
	      strcpy(ConnPassword, ROConnPassword);
	      MD5Sum(conn->authstr, p);
	      strcpy(ConnPassword, B);
	      if (!strcmp(p, conn->IProtoPacket)) {
		conn->authenticated = 1;
	        if (conn->handlers->AuthSuccess) conn->handlers->AuthSuccess(conn);
		break;
	      }
	    }

	    conn->authenticated = 0;
	    if (conn->handlers->AuthFailed) conn->handlers->AuthFailed(conn);
	  } else {
	    conn->authenticated = 2;
	    if (conn->handlers->AuthSuccess) conn->handlers->AuthSuccess(conn);
	  }
	  }
	  break;
	case 0x08: /* cram failed */
	  if (conn->handlers->AuthFailed) {
	    conn->handlers->AuthFailed(conn);
	    break;
	  }
	  break;
	default:
	  break;
      }

      conn->IProtoPacket--;
      break;

    case IPR_DC3:
      if (!conn->IProtoPacket) break; /* nothing arrived */
      conn->IProtoPacket = realloc(conn->IProtoPacket,
	                           conn->IProtoPacketLen + 1);
      conn->IProtoPacket[conn->IProtoPacketLen] = '\0';
      conn->IProtoPacket++;
 
      switch ((unsigned char) conn->IProtoPacket[-1]) {
	case 0xff: /* version */
	  if (conn->handlers->ASKVersion) {
	    conn->handlers->ASKVersion(conn, conn->IProtoPacket);
	    break;
	  }
	  IProtoSEND(conn, 0xff, "5.0");
	  break;
	case 0x01: /* user */
	  if (conn->handlers->ASKUser) {
	    conn->handlers->ASKUser(conn, conn->IProtoPacket);
	    break;
	  }
	  IProtoSEND(conn, 0x01, getenv("USER")); /* TODO: Do something better. */
	  break;
	case 0x40: /* prompt request */
	  if (conn->handlers->ASKPrompt) {
	    conn->handlers->ASKPrompt(conn, conn->IProtoPacket);
	    break;
	  }
	  IProtoSEND(conn, 0x80, "");
	  break;
	case 0x41: /* login prompt request */
	  if (conn->handlers->ASKLoginPrompt) {
	    conn->handlers->ASKLoginPrompt(conn, conn->IProtoPacket);
	    break;
	  }
	  IProtoSEND(conn, 0x80, "");
	  break;
	case 0x42: /* cancel command */
	  if (conn->handlers->ASKCancelPrompt) {
	    conn->handlers->ASKCancelPrompt(conn, conn->IProtoPacket);
	    break;
	  }
	  IProtoSEND(conn, 0x80, "");
	  break;
	case 0x43: /* takeover */
	  if (conn->handlers->ASKTakeOver) {
	    conn->handlers->ASKTakeOver(conn, conn->IProtoPacket);
	    break;
	  }
	  IProtoSEND(conn, 0x80, "");
	  break;
	case 0x02: /* cram */
	  {
	  char p[129], *d = StripData(conn->IProtoPacket, ",.:;@-");
	  if (conn->handlers->ASKCRAM) {
	    conn->handlers->ASKCRAM(conn, conn->IProtoPacket, d);
	    break;
	  }

	  MD5Sum(conn->IProtoPacket, p);
	  IProtoSEND(conn, 0x07, p);
	  break;
	  }
	case 0x3f: /* burst me */
	  if (conn->handlers->ASKBurstMe) {
	    char *d = StripData(conn->IProtoPacket, ",.:;@-");

	    conn->handlers->ASKBurstMe(conn, conn->IProtoPacket, d);
	    break;
	  }
	  IProtoSEND(conn, 0x80, "");
	  break;
	default:
	  IProtoSEND(conn, 0x80, "");
	  break;
      }
     
      conn->IProtoPacket--;
      break;

    case IPR_DC4:
      break;
  }

  conn->IProtoState = Chr - DC1 + 1;

  free(conn->IProtoPacket);
  conn->IProtoPacket = NULL;
  conn->IProtoPacketLen = 0;
}

void
TestIProtoChar(struct connection *conn, char Chr)
{
  if (Chr >= DC1 && Chr <= DC4) {
    ProcessIProtoChar(conn, Chr);
  } else if (conn->IProtoState == IPR_HANDSHAKE
	  || conn->IProtoState == IPR_DC1
	  || conn->IProtoState == IPR_DC4) {
    if (conn->IProtoState == IPR_HANDSHAKE) {
      /* We're in handhsake but didn't get DC1. Bah. */
      if (conn->handlers->AuthFailed) conn->handlers->AuthFailed(conn);
      conn->authenticated = 0;
    }
    conn->handlers->RecvChar(conn, Chr);
  } else {
    conn->IProtoPacket = ShrinkBuffer(conn->IProtoPacket, conn->IProtoPacketLen);
    conn->IProtoPacket[conn->IProtoPacketLen++] = Chr;
  }
}
