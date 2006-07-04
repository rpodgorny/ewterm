/* exch.c - Talking with ewrecv/exchange */

#include <stdlib.h>
#include <sys/ioctl.h>
#include <curses.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>

#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <linux/fd.h>

#include "ewterm.h"

#include "ascii.h"
#include "iproto.h"
#include "mml.h"
#include "version.h"

/* unfortunately we still cannot relax and free ourselves from the burden of
 * this insane protocol, because we will want to send acks to get prompt - see
 * ewrecv.c for protocol description */

/* How the thing PROBABLY works:
 * 
 * - We receive text in variably sized semilogical chunks, started by no
 * special sequence, terminated by ETX (^C). We acknowledge each this chunk
 * with BEL (^G).
 * 
 * - Sometimes, the chunk is started by ACK (^F) (obviously still being
 * terminated by ETX). It can be followed by ENQ (^E), then it's live check,
 * see below. Otherwise, it is start of a prompt string, and after the end of
 * this prompt chunk, user input is expected.
 * 
 * - If we want to provoke prompt, we send ACK (^F). Then, prompt chunk is
 * being sent by the exchange.
 * 
 * - Sometimes, exchange checks if we're still there - it sends ENQ (it's
 * probably always after ACK, but we're not sure) and we reply by NAK(^U)000^C
 * (original terminal has variable reply here, but we don't know what and why
 * and how and it looks that it works this way as well).
 * 
 * - We send EOT (^D) in order to cancel actual foreground command. */


struct connection *connection = NULL;

unsigned char ConvertMode = CV2CAPS;

char InBurst = 0;	/* In burst? */

/* Input request is special state of exchange, when it accepts something
 * different than ordinary commands ;-) - like when completing commands
 * (=<ETX>) and when deploying special prompt (*). And spec. for the second
 * case we have to be able to accept prompts even in logged off state and send
 * them when INPREQUEST is set, even when LOGGEDOFF is set also.
 */

int LoggedOff = 1, InputRequest = 0;

char *PendingCmd = NULL;
char LastCmd[1024] = "";

char ActPasswd[30], ActCommonPasswd[30];
char ActUsrname[10];
char ActOMT[10], ActExchange[10];
int ActJob = 0;
int LastMask = 0;
int WeKnowItIsComing = 0;

void LogOff(), GetJobData();

struct user {
  char *user;
  char *host;
  int id;
  int flags;
  struct user *next;
  struct user *prev;
};
struct user *user;

#define foreach_user	if (user) { struct user *cxx = user; do { struct user *usr = cxx;
#define foreach_user_end	cxx = cxx->next; } while (cxx != user); }


void (*InputRequestHook)() = NULL;
void (*NoInputRequestHook)() = NULL;
void (*CancelHook)() = NULL;


/* Changes logon/logoff text in the menu */
void RefreshLogTxt()
{
  if (LoggedOff) {
    MainMenu[8].Text = "LogOn";
    MainMenu[8].Addr = StartLogOn;
  }
  else {
    MainMenu[8].Text = "LogOff";
    MainMenu[8].Addr = LogOff;
  }
  RedrawKeys();
  RedrawStatus();
}

int SilentSendChar = 0;

void
SendChar(char Chr) {
  if (! SilentSendChar && (Chr >= 32 || Chr == 10 || Chr == 13)) {
    if (FilterFdIn >= 0 && FilterFdOut >= 0) {
      if (Chr == '^') AddToFilterQueue('^');
      AddToFilterQueue(Chr);
    } else {
      AddCh(Chr);
    }
  }

  pdebug("SendChar() %c/%x \n", Chr, Chr);
  
  if (connection) Write(connection, &Chr, 1);
}

void
ESendChar(char Chr) {
  SilentSendChar = 0;
  SendChar(Chr);
}

void
ColCmd()
{
  if (UsingColor) {
    wattron(CUAWindow, ATT_CMD);
    SetBright(CUAWindow, COL_CMD);
  }
  ActCol = COL_CMD;
}

/* Prompt detected (finished prompt chunk) */
void ProcessPrompt()
{
  if (InBurst || (connection && connection->fwmode == FWD_IN))
    /* prompt handling forbidden in burst */
    return;

  if (FilterFdIn >= 0 && FilterFdOut >= 0) {
    AddToFilterQueue('^');
    AddToFilterQueue('C');
  } else {
    ColCmd();
  }

  if (!InputRequest) {
    if (PendingCmd) {
      /* Send command in the queue */
      pdebug("ProcessPrompt() sending command %s\n", PendingCmd);
      AddStr(PendingCmd, ConvertMode, 1);
      AddStr("\n", 0, 1);
      strcpy(LastCmd, PendingCmd);
      free(PendingCmd);
      PendingCmd = NULL;
    } else {
      /* Dummy command for first prompt */
      AddStr("STATSSP:OST=UNA;\n", 0, 1);
    }
  }

  if (CUAWindow) RedrawStatus();
  RefreshLogTxt();
}

void
CancelCommand() {
  char cmd[256];

  if (!connection) return;

  ShadowHelp = 0;

  if (CancelHook) {
    CancelHook();
    return;
  }

  if (connection->fwmode == FWD_IN) {
    AddEStr("This connection is now for observation only.\n", 0, 0);
    return;
  }

  if (InputRequest) {
    InputRequest = 0;
    IProtoASK(connection, 0x42, NULL);
    return;
  }

  if (!ActJob) {
    AddEStr("Nothing to cancel!\n", 0, 0);
    return;
  }

  PendingCmd = NULL; /* we are more important. definitively. */

  pdebug("LastCmd %s\n", LastCmd);
  if (!strncasecmp(LastCmd, "DISP", 4) || !strncasecmp(LastCmd, "STAT", 4)) {
    sprintf(cmd, "STOPDISP:JN=%d;\n", ActJob);
    AddCommandToQueue(cmd, 2);
  } else
  if (!strncasecmp(LastCmd, "EXECCMDFILE", 11)) {
    sprintf(cmd, "STOPJOB:JN=%d;\n", ActJob);
    AddCommandToQueue(cmd, 2);
  } else
  {
    AddEStr("Cancel action not specified!\n", 0, 0);
  }
}

void
ForceCommand() {
  if (!connection) return;

  if (connection->fwmode == FWD_IN) {
    AddEStr("This connection is now for observation only.\n", 0, 0);
    return;
  }

  if (PendingCmd) {
    SendChar(ACK); InputRequest = 1; /* XXX: MOVE MOVE MOVE! */
    ProcessPrompt();

    if (PendingCmd) {
      /* bye, evil command :P */
      /* we don't get ever here, do we? */
      strcpy(LastCmd, PendingCmd);
      free(PendingCmd); PendingCmd = NULL;
    }
  } else
    AddEStr("Nothing to force!\n", 0, 0);
}

void DoLogOff()
{
  ActUsrname[0] = 0;
  ActPasswd[0] = 0;     /* Clear password when logon invoked by user */

  /* I hope this won't break anytime. --pasky */
  if (InputRequest) CancelCommand(); /* Quit from EDTS8 ;))) */
  AddCommandToQueue("ENDSESSION;", 2);

  RefreshLogTxt();
}

void ProceedLogOff()
{
  IProtoASK(connection, 0x43, NULL);
  WeKnowItIsComing = 1;
  /* We let ewrecv to confirm this, and we will automatically log ourselves
   * out after that. */
#if 0
  connection->fwmode = FWD_INOUT; /* for addcommandtoqueue */
  DoLogOff();
#endif
}

void LogOff()
{
  if (!connection) return;
  if (connection->fwmode == FWD_IN) {
    AddEStr("Warning, you are in observation mode and want to log off someone other!\n", 0, 0);
    AskBool("Proceed", ProceedLogOff, NULL);
  } else
    DoLogOff();
}

void ELogOff()
{
  if (!connection) return;
  if (connection->fwmode == FWD_IN) {
    AddEStr("This connection is now for observation only.\n", 0, 0);
    return;
  }

  /* Emergency logoff.. clear queue, reset commandmode etc. */

  /* Clear state */
  LoggedOff = 0; InputRequest = 0;
  ActJob = 0;
  /* Throw away pending cmd */
  if (PendingCmd) free(PendingCmd); PendingCmd = NULL;

  /* Ask for prompt by ourselves */
  /* XXX: MOVE? */
  SendChar(ACK);
  InputRequest = 1;

  LogOff();
}

void StopLogOn()
{
  InputRequest = 1; CancelCommand();
  RefreshLogTxt();
}

void DoSendPassword()
{
  char *TmpPtr;

  InputRequest = 0;
  ShadowHelp = 0;
  TmpPtr = ActPasswd;
  if (*TmpPtr) exit;
  SilentSendChar = 1;
  SendChar(DC4);
  while(*TmpPtr) SendChar(*TmpPtr++);
  SendChar(DC1);
  ESendChar(13);
  ESendChar(10);
}

void ChangePassword2()
{
  DisplayMode |= HIDE;
  beep();
  InputRequest = 1;
  ShadowHelp = 1;
  GetString(ActPasswd, 29, "Password", DoSendPassword, CancelCommand);
}

void DoSendCommonPassword()
{
  char *TmpPtr;

  ShadowHelp = 0;
  InputRequest = 0;
  TmpPtr = ActCommonPasswd;
  if (*TmpPtr) exit;
  SilentSendChar = 1;
  SendChar(DC4);
  while(*TmpPtr) SendChar(*TmpPtr++);
  SendChar(DC1);
  ESendChar(13);
  ESendChar(10);
}

void ChangeCommonPassword()
{
  DisplayMode |= HIDE;
  beep();
  ActCommonPasswd[0] = 0;
  InputRequest = 1;
  ShadowHelp = 1;
  GetString(ActCommonPasswd, 29, "Password", DoSendCommonPassword, CancelCommand);
}

void DoSendUsername()
{
  char *TmpPtr;

  InputRequest = 0;
  ShadowHelp = 0;
  TmpPtr = ActUsrname;
  if (*TmpPtr) exit;
  while(*TmpPtr) ESendChar(toupper(*TmpPtr++));
  ESendChar(13);
  ESendChar(10);
}

void ChangeUsername2()
{
  beep();
  InputRequest = 1;
  ShadowHelp = 1;
  GetString(ActUsrname, 9, "Username", DoSendUsername, CancelCommand);
}

void SendUsername()
{
  if (ActUsrname[0] == 0) ChangeUsername2();
  else DoSendUsername();
}

void SendPassword()
{
  if (ActPasswd[0] == 0) ChangePassword2();
  else DoSendPassword();
}

void SendCommonPassword()
{
  ChangeCommonPassword();
}


void StartLogOn()
{
  if (!connection) return;

  if (connection->fwmode == FWD_IN) {
    AddEStr("This connection is now for observation only.\n", 0, 0);
    return;
  }

  MainMenu[8].Text = "Stop";
  MainMenu[8].Addr = StopLogOn;
  RedrawKeys();
  IProtoASK(connection, 0x41, NULL);
}

void
DispOMTUser(struct mml_command *mml)
{
  struct mml_param params[2] = {
    { "USER", NULL },
    { "ID", NULL },
  };
  int m = 0;
  int id = -1;
  char s[256];

  sort_mml_command(mml, 2, params);
  if (params[1].value) id = atoi(params[1].value);

  InBurst = 1;
  AddEStr("\n\n\n\n\n", 0, 0);
  {
    time_t ttime = time(NULL);
    struct tm *tm = localtime(&ttime);
    snprintf(s, 256, "%-52s  %02d-%02d-%02d  %02d:%02d:%02d\n",
	     "OMT/EWTERM/" VERSION,
	     tm->tm_year % 100, tm->tm_mon + 1, tm->tm_mday,
	     tm->tm_hour, tm->tm_min, tm->tm_sec);
    AddEStr(s, 0, 0);
  }
  AddEStr("\n\n", 0, 0);
  snprintf(s, 256, "%-64s  EXEC'D\n\n", mml->command);
  AddEStr(s, 0, 0);
  AddEStr("USER     HOST                 ID    FLAGS\n", 0, 0);
  AddEStr("--------+--------------------+-----+-----\n", 0, 0);
  foreach_user {
    if ((!params[0].value || !strcasecmp(params[0].value, usr->user)) &&
	(id < 0 || id == usr->id)) {
      char s[256];
      char f[6] = "-----";
      if (usr->flags & 1) f[0] = 'Y';
      snprintf(s, 256, "%-8s %-20.20s %-5d %s\n", usr->user, usr->host, usr->id, f);
      AddEStr(s, 0, 0);
      m = 1;
    }
  } foreach_user_end;
  if (!m)
    AddEStr("NO (MORE) DATA FOR DISPLAY AVAILABLE\n", 0, 0);
  AddEStr("\n", 0, 0);
  AddEStr("END JOB EXEC'D\n\n", 0, 0);
  InBurst = 0;
}

void
EnterOMTMsg(struct mml_command *mml)
{
  struct mml_param params[3] = {
    { "USER", NULL },
    { "ID", NULL },
    { "MSG", NULL },
  };
  int id = -1;
  char s[1024];
  char *msg = NULL;

  sort_mml_command(mml, 3, params);
  if (params[2].value) msg = dequote(params[2].value);
  if (params[1].value) id = atoi(params[1].value);

  if (!msg) {
    char s[256];
    snprintf(s, 256, "%-60s  NOT EXEC'D\n", "Missing mandatory MSG parameter.");
    AddEStr("\n", 0, 0);
    AddEStr(s, 0, 0);
    return;
  }

  InBurst = 1;

  if (!params[0].value)
    snprintf(s, 1024, "[ewrecv] [->ALL] %s\n", msg);
  else
    if (id < 0)
      snprintf(s, 1024, "[ewrecv] [->%s@*:*] %s\n", params[0].value, msg);
    else
      snprintf(s, 1024, "[ewrecv] [->%s@*:%d] %s\n", params[0].value, id, msg);
  AddEStr(s, 0, 0);

  snprintf(s, 1024, "%s@:%d=%s", params[0].value?params[0].value:"", id, msg);
  if (connection) IProtoSEND(connection, 0x03, s);
  else {
    char s[256];
    snprintf(s, 256, "%-60s  NOT EXEC'D\n", "Not connected to server.");
    AddEStr("\n", 0, 0);
    AddEStr(s, 0, 0);
    return;
  }

  InBurst = 0;

  free(msg);
}

void AddCommandToQueue(char *Command, char Convert)
{
  struct mml_command *mml;
  pdebug("AddCommandToQueue() %s (|%c)\n", Command, Convert);

  if (strchr(Command, 13)) *strchr(Command, 13) = 0;
  if (strchr(Command, 10)) *strchr(Command, 10) = 0;
  mml = parse_mml_command(Command);

  if (!strcasecmp(mml->command, "DISPOMTUSER")) {
    DispOMTUser(mml);
    free_mml_command(mml);
    return;
  } else if (!strcasecmp(mml->command, "ENTROMTMSG")) {
    EnterOMTMsg(mml);
    free_mml_command(mml);
    return;
  }

  free_mml_command(mml);

  if (connection && connection->fwmode == FWD_IN) {
    AddEStr("This connection is now for observation only.\n", 0, 0);
    return;
  }

  if (!InputRequest && PendingCmd) {
    AddEStr("Some command is already queued!\n", 0, 0);
  } else {
    /* Add to queue */

    if (connection && LoggedOff && !InputRequest) {
      if (AutoLogOn == '1') {
        StartLogOn();
      } else {
	AddEStr("You are not logged in!\n", 0, 0);
	return;
      }
    }

    if (PendingCmd) free(PendingCmd);
    PendingCmd = strdup(Command);

    if (!connection || InputRequest) {
      InputRequest = 0;
      ProcessPrompt();
    } else
      IProtoASK(connection, 0x40, NULL);
  }
}

void CheckChr(struct connection *c, int Chr)
{ 
  static WINDOW *StatWindow;
  static PANEL *StatPanel;

  if (Chr < 32 && Chr != 9 && Chr != 10) {
    if (Chr == SO) {
      InBurst = 1;
      DisplayMode |= OTHERWINDOW;
      NewWindow(24, 1, "Status", &StatWindow, &StatPanel);
      mvwaddstr(StatWindow, 1, 1, "Please wait, bursting...");
      wnoutrefresh(StatWindow);
      PushEditOptions();

    } else if (Chr == SI) {
      PopEditOptions();
      del_panel(StatPanel);
      delwin(StatWindow);
      DisplayMode &= ~OTHERWINDOW;
      InBurst = 0;
    }
  } else {
    /* When something came from the exchange, there can't be pending input
     * request anymore. */
    InputRequest = 0;
    if (FilterFdIn >= 0 && FilterFdOut >= 0) {
      /* ^ has a special meaning so double it. */
      if (Chr == '^') AddToFilterQueue('^');
      AddToFilterQueue(Chr);
    } else {
      AddCh(Chr);
    }
  }
}


void
GotNotify(struct connection *c, char *msg)
{
  char str[1024];

  snprintf(str, 1024, "[ewrecv] [notify] %s\n", msg);
  AddEStr(str, 0, 0);
}

void
GotPrivMsg(struct connection *c, char *from, int id, char *host, char *msg, char *d)
{
  char str[1024];

  snprintf(str, 1024, "[ewrecv] [%s@%s:%d->] %s\n", from, host, id, msg);
  AddEStr(str, 0, 0);
}

void
GotFwMode(struct connection *c, enum fwmode fwmode, char *d)
{
  if (fwmode == FWD_IN)
    AddEStr("You are now in observer forwarding mode.\n", 0, 0);
  else if (fwmode == FWD_INOUT) {
    AddEStr("You are now in master forwarding mode.\n", 0, 0);
    if (!LoggedOff) {
      if (!WeKnowItIsComing) {
      AddEStr("Lo, face the dark part of ewterm. Escaping from les pays inconnus,\n", 0, 0);
      AddEStr("where only fear and despair lies. Let the sunset lead us.\n", 0, 0);
      } else WeKnowItIsComing = 0; /* FIXME? */
      DoLogOff();
    }
  } else
    AddEStr("You are now in unknown forwarding mode.\n", 0, 0);
  RedrawStatus();
}

void
GotUserConnect(struct connection *c, int flags, char *user_, char *host, char *id_, char *d)
{
  char str[1024];
  int id = atoi(id_);
  struct user *usr = malloc(sizeof(struct user));

  usr->user = strdup(user_); usr->id = id; usr->host = strdup(host); usr->flags = flags;

  if (user) {
    user->prev->next = usr;
    usr->prev = user->prev;
    user->prev = usr;
    usr->next = user;
  } else {
    user = usr;
    user->next = usr;
    user->prev = usr;
  }

  if (!(flags & 1)) {
    snprintf(str, 1024, "[ewrecv] [%s@%s:%d] CONNECT\n", user_, host, id);
    AddEStr(str, 0, 0);
  }
}

void
GotUserDisconnect(struct connection *c, char *user_, char *host, char *id_, char *d)
{
  char str[1024];
  int id = atoi(id_);

  foreach_user {
    if (!strcasecmp(user_, usr->user) && id == usr->id) {
      if (usr == user)
	user = usr->next;
      if (usr == user)
	user = NULL;
      usr->prev->next = usr->next;
      usr->next->prev = usr->prev;
      free(usr->user);
      free(usr);
      break;
    }
  } foreach_user_end;

  snprintf(str, 1024, "[ewrecv] [%s@%s:%d] DISCONNECT\n", user_, host, id);
  AddEStr(str, 0, 0);
}

void
ColPrompt()
{
  ActCol = COL_PROMPT;
  if (UsingColor) {
    wattron(CUAWindow, ATT_PROMPT);
    SetBright(CUAWindow, COL_PROMPT);
  }
}

void
GotPromptStart(struct connection *c, char *d) {
  if (FilterFdIn >= 0 && FilterFdOut >= 0) {
    AddToFilterQueue('^');
    AddToFilterQueue('P');
  } else {
    ColPrompt();
  }
}

void
ColTerm()
{
  ActCol = COL_TERM;
  if (UsingColor) {
    wattron(CUAWindow, ATT_TERM);
    SetBright(CUAWindow, COL_TERM);
  }
}

void
GotPromptEnd(struct connection *c, char type, char *job, char *d) {
  if (FilterFdIn >= 0 && FilterFdOut >= 0) {
    AddToFilterQueue('^');
    AddToFilterQueue('T');
    AddToFilterQueue('\r');
  } else {
    ColTerm();
  }

  switch (type) {
    case 'I': InputRequest = 1;
    case '<': if (type == '<' && *job) {
		ActJob = atoi(job);
	      }
	      RedrawStatus();
	      ProcessPrompt();
	      if (InputRequest) {
		if (InputRequestHook)
		  InputRequestHook();
	      } else {
		if (NoInputRequestHook)
		  NoInputRequestHook();
	      }
              break;
    case 'U': if (connection->fwmode == FWD_INOUT)
	        SendUsername();
	      break;
    case 'P': if (connection->fwmode == FWD_INOUT)
		SendPassword();
	      break;
    case 'p': if (connection->fwmode == FWD_INOUT)
		SendCommonPassword();
	      break;
    case 'F': if (connection->fwmode == FWD_INOUT) {
		if (SendFilePassword == '1')
		  SendCommonPassword();
	        else
		  AddStr("\n", 0, 1);
	      }
	      break;
  }
}

void
GotLoginError(struct connection *c, char *d)
{
  ActUsrname[0] = 0; ActPasswd[0] = 0;
  LoggedOff = 1;
  RefreshLogTxt();
  RedrawStatus();
}

void
GotLoginSuccess(struct connection *c, char *d)
{
  LoggedOff = 0;
  RefreshLogTxt();
}

void
GotLogout(struct connection *c, char *d)
{
  if (LastMask == 7) ActUsrname[0] = 0; /* XXX */
  ActPasswd[0] = 0;
  LoggedOff = 1;
  RefreshLogTxt();
}

void
GotJob(struct connection *c, char *job, char *d)
{
  if (atoi(job) == ActJob) {
    ActJob = 0;
    RedrawStatus();
  }
}

void
GotMask(struct connection *c, char *mask, char *d)
{
  if (LastMask != atoi(mask)) {
    LastMask = atoi(mask);
    RedrawStatus();
  }
}

void
GotHeader(struct connection *c, char *job, char *omt, char *uname, char *exchange, char *d)
{
  strcpy(ActExchange, exchange);
  if (atoi(job) == ActJob && *uname && *uname != 0 && c->fwmode != FWD_IN && !LoggedOff) strcpy(ActUsrname, uname);
  if (!strcasecmp(ActUsrname, uname)) {
    strcpy(ActOMT, omt);
  }
  RedrawStatus();
}

void
AskForBurst()
{
  char l[128] = "";

  if (BurstLines >= 0) snprintf(l, 128, "%d", BurstLines);

  IProtoASK(connection, 0x3f, l);
}

void SendCRAMPassword()
{
  char p[128];

  ShadowHelp = 0;
  MD5Sum(connection->authstr, p);
  IProtoSEND(connection, 0x07, p);
  free(connection->authstr), connection->authstr = NULL;

  UpdateOptions(); /* remember ConnPassword */

  AskForBurst();
}

void
GotCRAM(struct connection *c, char *token, char *d)
{
  char p[128];

  if (!*ConnPassword) {
    c->authstr = strdup(token);
    DisplayMode |= HIDE;
    beep();
    ShadowHelp = 1;
    GetString(ConnPassword, 29, "Receiver Password", SendCRAMPassword, NULL /* TODO: disconnect? */);
    return;
  }
  MD5Sum(token, p);
  IProtoSEND(c, 0x07, p);
  AskForBurst();
}

void
AuthFailed(struct connection *c)
{
  AddEStr("Receiver login failed.", 0, 0);
  /* Reconnect! */
  Reconnect = 1;

  *ConnPassword = 0;
  UpdateOptions();
}

struct connection *
MkConnection(int SockFd)
{
  static struct conn_handlers h = {
    /* 2.1a */
    (int(*)(struct connection *, char)) CheckChr,
    NULL,
    NULL,
    GotNotify,
    NULL,

    /* 2.1b */
    GotFwMode,
    GotUserConnect,
    GotUserDisconnect,

    /* 2.2a */
    GotPromptStart,
    GotPromptEnd,
    GotLoginError,
    GotLoginSuccess,
    GotLogout,
    GotJob,
    GotMask,
    GotHeader,

    /* 2.3a */
    GotPrivMsg,

    /* 2.1a */
    NULL,
    NULL,

    /* 2.2a */
    NULL,
    NULL,
    NULL,
    NULL,

    /* 0.5pre3 */
    GotCRAM,

    /* 0.5rc2 */
    NULL,

    /* 0.5pre3 */
    NULL,
    AuthFailed,
  };
  connection = MakeConnection(SockFd, &h);
  return connection;
}

void
AttachConnection()
{
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
      exit(6);
    }
    addr.sin_addr.s_addr = ((struct in_addr *) host->h_addr)->s_addr;
  }
  addr.sin_port = htons(HostPort);

  if (connect(SockFd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
    perror("connect()");
    exit(6);
  }
  connection = MkConnection(SockFd);

  if (!connection) {
    fprintf(stderr, "Unable to create connection!\n");
    exit(9);
  }
}
