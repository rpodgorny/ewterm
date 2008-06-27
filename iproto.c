#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ascii.h"
#include "iproto.h"
#include "md5.h"

#include "ewterm.h"

char ConnPassword[128];
char ROConnPassword[128];

#define BUF_GRAIN 256

void *ShrinkBuffer(void *Buf, int Len) {
	//printf(":::SHRINK %x %d\n\n", Buf, Len);

	if (!(Len % BUF_GRAIN)) {
		Buf = realloc(Buf, Len + BUF_GRAIN);
	}
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

	conn->destroy = 0;

	conn->alarms = 0;

	return conn;
}

void FreeConnection(struct connection *conn) {
	if (conn->host) free(conn->host);
	if (conn->user) free(conn->user);
	if (conn->authstr) free(conn->authstr);
	if (conn->IProtoPacket) free(conn->IProtoPacket);
	if (conn->ReadBuffer) free(conn->ReadBuffer);
	if (conn->WriteBuffer) free(conn->WriteBuffer);
	if (conn->X25WriteBuf) free(conn->X25WriteBuf);
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
	if (!dlen) return;

	conn->WriteBuffer = ShrinkBuffer(conn->WriteBuffer, conn->WriteBufferLen);
	int len = BUF_GRAIN - (conn->WriteBufferLen % BUF_GRAIN);
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

	if (str[idx]) {
		str[idx] = 0;
		return str + idx + 1;
	}
	
	return NULL; 
}

void MD5Sum(char *v, char *p) {
	struct md5_state_s ms;
	md5_byte_t digest[16]; int i;

	md5_init(&ms);
	md5_append(&ms, v, strlen(v));
	md5_append(&ms, ConnPassword, strlen(ConnPassword));
	md5_finish(&ms, digest);

	for (i = 0; i < 16; i++) {
		snprintf(p + (i*2), 3, "%02x", digest[i]);
	}
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
	case 0x48: /* alarms sending starts */
	  if (conn->handlers->SENDAlarmsOn) {
	    conn->handlers->SENDAlarmsOn(conn, conn->IProtoPacket);
	    break;
	  }
	  break;
	case 0x49: /* alarms sending ends */
	  if (conn->handlers->SENDAlarmsOff) {
	    conn->handlers->SENDAlarmsOff(conn, conn->IProtoPacket);
	    break;
	  }
	  break;
	case 0x50: /* exchange list */
	  if (conn->handlers->SENDExchangeList) {
	    conn->handlers->SENDExchangeList(conn, conn->IProtoPacket, NULL);
	    break;
	  }
	  break;
	case 0x51: /* connection id */
	  if (conn->handlers->SENDConnectionId) {
	  	int id = atoi(conn->IProtoPacket);
	    conn->handlers->SENDConnectionId(conn, id, NULL);
	    break;
	  }
	  break;
	case 0x52: /* attach status */
	  if (conn->handlers->SENDAttach) {
	  	int status = atoi(conn->IProtoPacket);
	    conn->handlers->SENDAttach(conn, status, NULL);
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
	    conn->handlers->ASKLoginPrompt(conn, conn->IProtoPacket, NULL);
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
	case 0x44: /* start sending alarms */
	  if (conn->handlers->ASKAlarmsOn) {
	    conn->handlers->ASKAlarmsOn(conn, conn->IProtoPacket);
		break;
	  }
	  IProtoSEND(conn, 0x80, "");
	case 0x45: /* stop sending alarms */
	  if (conn->handlers->ASKAlarmsOff) {
	    conn->handlers->ASKAlarmsOff(conn, conn->IProtoPacket);
		break;
	  }
	  IProtoSEND(conn, 0x80, "");
	case 0x46: /* logout request */
	  if (conn->handlers->ASKLogout) {
	    conn->handlers->ASKLogout(conn, conn->IProtoPacket);
		break;
	  }
	  IProtoSEND(conn, 0x80, "");
	case 0x50: /* exchange list request */
	  if (conn->handlers->ASKExchangeList) {
        conn->handlers->ASKExchangeList(conn, conn->IProtoPacket);
        break;
      }
      IProtoSEND(conn, 0x80, "");
	case 0x51: /* cancel job request */
	  if (conn->handlers->ASKCancelJob) {
        conn->handlers->ASKCancelJob(conn, conn->IProtoPacket);
        break;
      }
      IProtoSEND(conn, 0x80, "");
	case 0x52: /* detach request */
	  if (conn->handlers->ASKDetach) {
        conn->handlers->ASKDetach(conn, conn->IProtoPacket);
        break;
      }
      IProtoSEND(conn, 0x80, "");
	case 0x53: /* attach request */
	  if (conn->handlers->ASKAttach) {
		int id = strtol(conn->IProtoPacket, NULL, 10);
        conn->handlers->ASKAttach(conn, id, conn->IProtoPacket);
        break;
      }
      IProtoSEND(conn, 0x80, "");
	case 0x54: /* get connection id request */
	  if (conn->handlers->ASKConnectionId) {
        conn->handlers->ASKConnectionId(conn, conn->IProtoPacket);
        break;
      }
      IProtoSEND(conn, 0x80, "");

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

void TestIProtoChar(struct connection *conn, char Chr) {
	pdebug("TestIProtoChar() %c/x%x\n", Chr, Chr);

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
