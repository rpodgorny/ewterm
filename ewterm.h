/* ewterm.h - container */

#include <curses.h>
#include <panel.h>

#include "iproto.h"

/* Some types */

typedef unsigned char byte;
typedef unsigned long ulg;

/* promiscuous buffer */
extern unsigned char MultiBuf[];

/* evil stuff */
#define del_panel(panel_) { del_panel(panel_); panel_ = NULL; }
//#define	DEBUG
#ifdef DEBUG
FILE *debugf;
#define pdebug(fmt...) { fprintf(debugf, fmt); fflush(debugf); }
#else
#define pdebug(fmt...)
#endif

/* ewterm.c - base */
extern void DoneQuit();
extern int MainLoop;
extern char HostName[], HostPortStr[];
extern unsigned int HostPort;
extern char BurstLinesStr[];
extern int BurstLines;
extern int Reconnect;

/* bfu.c - UI commons and miscs */
extern WINDOW *MainW, *CUAWindow, *InfoWindow;
extern PANEL *CUAPanel, *InfoPanel;
extern int CUALines, HelpY, EditY, ActKey;
extern char UpdateCUAW, IsNonModal, ShadowHelp;
extern void (*EditHook)(), (*EnterHook)(), (*UpHook)(), (*DownHook)(),
	    (*PgUpHook)(),(*PgDownHook)(),(*HomeHook)(),(*EndHook)(),
	    (*TabHook)(),  (*CharHook)();
extern void RecreateWindows();
extern void DrawBox(WINDOW *, int, int, int, int);
extern void NewWindow(int, int, char *, WINDOW **, PANEL **);
extern void AskBool(char *, void (*)(), void (*)());
extern void InitScr(), AskQuit();
extern void Dummy();

/* buffer.c - Exchange output buffer */
extern struct THistE *LastBuf, *FirstBuf, *TopBuf, *CUATop;
extern int NumBuf, MaxLineNum;
#define LINE 0		/* exchange -> */
#define COMMAND 1	/* -> exchange */
extern unsigned char ContType;
extern char BufferSavePath[], BufSaveFName[];
extern void RedrawCUAWindow();
extern void AddLineToBuffer(char, char);
extern void AddCh(char);
extern void AddStr(char *, char, char), AddEStr(char *, char, char);
extern void StartShowBuf();

/* edit.c - Edit lines, user input handling */
#define CV2CAPS 1
#define ADDSEMI 2
#define OVERWRITE 4
extern WINDOW *ActEditWindow;
extern unsigned char ConvertMode;
extern char LineBuf[];
extern char *EditBuf;
extern int LineBLen, LineBPos, LineBSize, LineWSize, LineWOff;
extern int EditYOff, EditXOff;
extern void RefreshEdit();
extern void EditHome(), EditEnd(), DefPgUpHook(), CmdEnterHook(), BufHook();
extern int ProcessEscKey(int); extern void ProcessSpecialKey(int);

/* exch.c - Talking with exchange */
extern struct connection *connection;
extern int LoggedOff, InputRequest;
extern char *PendingCmd;
extern char ActPasswd[], ActUsrname[], ActOMT[], ActExchange[];
extern int ActJob, LastMask; extern char InBurst;
extern char ConnUsername[]; // TODO: move to somewhere else?
extern void (*InputRequestHook)();
extern void (*NoInputRequestHook)();
extern void (*CancelHook)();
extern void ESendChar(char);
extern void ForceCommand(), CancelCommand(), ELogOff(), StartLogOn();
extern void AlarmsOn(), AlarmsOff();
extern void AddCommandToQueue(char *, char);
extern void AttachConnection();

extern void ColCmd(), ColPrompt(), ColTerm();

/* frq.c - Filerequester */
extern char FRqDir[];
extern void FileRequest(char *, char *, void (*)());

/* gstr.c - GetString functions */
extern void GetStringDone(int);
extern void GetString(char *, int, char *, void (*)(), void (*)());

/* help.c - Help subsystem */
#define SetHelpColors(x...) /* was in agview */
extern char HelpFile[];
extern void ShowHelp(), MkHelpWin(), DelHelpWin();
WINDOW *HelpWindow;
PANEL *HelpPanel;

/* history.c - Command history */
#define	MAXNUMHIST 500
struct THistE {
  struct THistE *Next, *Prev;
  unsigned char Type, Color;
  int Number;
  char Data[0];		/* String of size Length+1 (includind \0). It is */
			/* ended with \n if it is shorter than screen width */
};
extern char HistoryMode[], CmdHistLenT[];
extern struct THistE *ActHist, *OldActHist, *FirstLine, *LastLine;
extern char ClearLastLine, DontGoUp;
extern int CmdHistLen, NumHist;
extern void PrevHistoryCommand(), NextHistoryCommand(), ShowHistoryMenu();
extern void LoadHistory(), SaveHistory();

/* lists.c - Generic lists handling */
struct TListEntry {
  struct TListEntry *Next, *Prev;
  char Data[0];
};
extern struct TListEntry *ListAct;
extern int ListActNum;
#define CMP_NONE 0 /* comparing in lists */
#define CMP_CASE 1
#define CMP_NOCASE 2
extern void InitList(WINDOW *, int, int, int, int, struct TListEntry *, int, int, char);
extern void ListDrawAct();
extern void ListControl(void (*)(), void (*)());

/* menu.c - Menu subsystem */
struct MenuEntry {
  char *Text;
  char Type;	/* 0 = Command, 1 = Submenu */
  void *Addr;
};
extern struct MenuEntry *ActMenu, MainMenu[];
#define STATUSLINE 1
#define HELPLINE 4
#define OTHERWINDOW 8
#define BUFSHOWN 16
#define HIDE 32		/* Print '*' instead of chars */
#define NOEDIT	64	/* Disables line editting */
#define HELPSHOWN 128
extern unsigned int DisplayMode;
extern void SetMenu(struct MenuEntry *, char);
extern void RedrawKeys(), RedrawStatus();

/* colors.c - Colourful stuff */
#define COL_TERM 0
#define COL_STATUS 1
#define COL_EDIT 1
#define COL_WIN 2
#define COL_BUF 3
#define COL_CMD 4
#define COL_PROMPT 5
#define COL_ERR 6
#define COL_HELP 7
#define COL_LINK 8
#define ATT_TERM (COL_TERM+1)<<8
#define ATT_STATUS (COL_STATUS+1)<<8
#define ATT_EDIT (COL_EDIT+1)<<8
#define ATT_WIN (COL_WIN+1)<<8
#define ATT_BUF (COL_BUF+1)<<8
#define ATT_CMD (COL_CMD+1)<<8
#define ATT_PROMPT (COL_PROMPT+1)<<8
#define ATT_ERR (COL_ERR+1)<<8
#define ATT_HELP (COL_HELP+1)<<8
#define ATT_LINK (COL_LINK+1)<<8
#define BrightAttr(Col) ((Br[Col][0] == '1')?A_BOLD:0)
#define FillLine(Win, Col) wchgat(Win, -1, ((Col+1)<<8)+BrightAttr(Col), Col+1, 0)
extern char UsingColor, DenyColors, ForceMono;
extern char Br[][2];
extern int FGs[], BGs[];
extern unsigned char ActCol;
extern struct MenuEntry ColorMenu[];
extern void SetBright(WINDOW *, int);
extern void CreatePairs(), StartShowColor();

/* options.c - Options */
struct OptionEntry {
  void *Next, *Prev;
  char Data[30];
};
WINDOW *OptWindow;
extern struct OptionEntry OptionList[];
extern char BadOpt;
extern char LogFName[], Profile[];
extern void PushEditOptions(), PopEditOptions(), UpdateOptions(), OptChoosed(), ShowOptions();
extern void ReadOptions(char *);
extern char AutoLogOn, SendFilePassword;

/* sendfile.c - Sending of files to exchange */
extern void SendFile();
extern char SendFileFName[], SendFilePath[];

/* filter.c - Sending stuff from the exchange through a filter */
extern int FilterFdIn, FilterFdOut, FilterQueueLen;
extern char *FilterQueue, FilterCmd[];
extern void InitFilter(), DoneFilter();
extern void AddToFilterQueue(char);

/* forms.c - Forms backend routines */
extern char FormsDir[];
