/* options.c - Options */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include <errno.h>
#include <unistd.h>
#include "ewterm.h"
#include "iproto.h"

#define OPTID "EWTERM OPTIONS V 1"

char BadOpt;

char CodedPasswd[70];

char Profile[256] = "";

void SwitchCaps(), SwitchSemi(), SwitchAutoLog(), SwitchLFL(), HideOptions();

char HelpFile[1024]; /* backwards compatibility */

struct MenuEntry OptionsMenu[10] = {
  {"Help", 0, ShowHelp},
  {"Caps", 0, SwitchCaps},
  {"NoSemi", 0, SwitchSemi},
  {"LogOn", 0, StartLogOn},
  {"AutoLOn", 0, SwitchAutoLog},
  {"LogFile", 0, SwitchLFL},
  {"ELogOff", 0, ELogOff},
  {"Colors", 0, StartShowColor},
  {"", 0, NULL},
  {"Back", 0, HideOptions}
};

struct OptionEntry OptionList[] = {
  {OptionList+1 , 0,             "Host                  "},
  {OptionList+2 , OptionList,    "Port                  "},
  {OptionList+3 , OptionList+1 , "Receiver Password     "},
  {OptionList+4 , OptionList+2 , "Username              "},
  {OptionList+5 , OptionList+3 , "Password              "},
  {OptionList+6 , OptionList+4 , "History limit         "},
  {OptionList+7 , OptionList+5 , "EWSD help directory   "},
  {OptionList+8 , OptionList+6 , "Pos in history: Bottom"},
  {OptionList+9 , OptionList+7 , "Always mono mode: No  "},
  {OptionList+10, OptionList+8 , "Filter command        "},
  {0		, OptionList+9 , "Burst lines           "},
};

void StartSetHost(), StartSetPort(), ChangeConnPassword(), ChangeUsername2(), ChangePassword();
void SetHistLimit(), SetHelpFile(), ChangeHistoryMode(), ChangeForceMonoMode(), SetFilterCommand();
void StartSetBurstLines();

void (*OptPtrs[])() = {
StartSetHost,
StartSetPort,
ChangeConnPassword,
ChangeUsername2,
ChangePassword,
SetHistLimit,
SetHelpFile,
ChangeHistoryMode,
ChangeForceMonoMode,
SetFilterCommand,
StartSetBurstLines,
};

struct TFKey {
  struct TFKey *Next, *Prev;
  char FNm[4];
  char Contents[71];
} FKeys[10];

char LogFName[256];

WINDOW *OptWindow;
PANEL *OptPanel;

struct {
  char ID[21];
  char *Buf;
  int Len;
} OptFmt[] = {
  {"USRNAME", ActUsrname, 30},
  {"PASSWD", CodedPasswd, 70},
  {"HOST", HostName, 50},
  {"PORT", HostPortStr, 50},
  {"RECVPASSWD", ConnPassword, 70},
  {"HISTLIMIT", CmdHistLenT, 30},
  {"HISTMODE", HistoryMode, 1},
  {"FORCEMONO", &ForceMono, 1},
  {"FKEY1", FKeys[0].Contents, 70},
  {"FKEY2", FKeys[1].Contents, 70},
  {"FKEY3", FKeys[2].Contents, 70},
  {"FKEY4", FKeys[3].Contents, 70},
  {"FKEY5", FKeys[4].Contents, 70},
  {"FKEY6", FKeys[5].Contents, 70},
  {"FKEY7", FKeys[6].Contents, 70},
  {"FKEY8", FKeys[7].Contents, 70},
  {"FKEY9", FKeys[8].Contents, 70},
  {"FKEY10", FKeys[9].Contents, 70},
  {"TERMBR", Br[0], 1},
  {"STATBR", Br[1], 1},
  {"WINBR", Br[2], 1},
  {"BUFBR", Br[3], 1},
  {"CMDBR", Br[4], 1},
  {"PROMPTBR", Br[4], 1},
  {"ERRBR", Br[5], 1},
  {"HELPBR", Br[6], 1},
  {"LINKBR", Br[7], 1},
  {"TERMFG", 0, 10},
  {"TERMBG", 0, 10},
  {"STATFG", 0, 10},
  {"STATBG", 0, 10},
  {"WINFG", 0, 10},
  {"WINBG", 0, 10},
  {"BUFFG", 0, 10},
  {"BUFBG", 0, 10},
  {"CMDFG", 0, 10},
  {"CMDBG", 0, 10},
  {"PROMPTFG", 0, 10},
  {"PROMPTBG", 0, 10},
  {"ERRFG", 0, 10},
  {"ERRBG", 0, 10},
  {"HELPFG", 0, 10},
  {"HELPBG", 0, 10},
  {"LINKFG", 0, 10},
  {"LINKBG", 0, 10},
  {"BUFFERSAVEPATH", BufferSavePath, 256},
  {"LOGFNAME", LogFName, 256},
  {"AUTOLOGON", &AutoLogOn, 1},
  {"SENDFILEPASSWORD", &SendFilePassword, 1},
  {"SENDFILEPATH", SendFilePath, 256},
  {"FILTERCMD", FilterCmd, 256},
  {"BURSTLINES", BurstLinesStr, 256},
  {"FORMSDIR", FormsDir, 256},
  {"", 0, 0}
};

int EditStack[16*4];
int EditSPtr;

char AutoLogOn = '0';
char SendFilePassword = '0';



void PushEditOptions()
{
  EditStack[EditSPtr++] = (int)CharHook;
  EditStack[EditSPtr++] = (int)TabHook;
  EditStack[EditSPtr++] = (int)EnterHook;
  EditStack[EditSPtr++] = (int)UpHook;
  EditStack[EditSPtr++] = (int)DownHook;
  EditStack[EditSPtr++] = (int)PgUpHook;
  EditStack[EditSPtr++] = (int)PgDownHook;
  EditStack[EditSPtr++] = (int)EditBuf;
  EditStack[EditSPtr++] = LineBLen;
  EditStack[EditSPtr++] = LineBPos;
  EditStack[EditSPtr++] = LineBSize;
  EditStack[EditSPtr++] = LineWSize;
  EditStack[EditSPtr++] = LineWOff;
  EditStack[EditSPtr++] = (int)ActEditWindow;
  EditStack[EditSPtr++] = EditXOff;
  EditStack[EditSPtr++] = EditYOff;
  EditStack[EditSPtr++] = (int)HomeHook;
  EditStack[EditSPtr++] = (int)EndHook;
  EditStack[EditSPtr++] = DisplayMode;

  DisplayMode |= OTHERWINDOW;
  DisplayMode &= 65535-NOEDIT;
}

void PopEditOptions()
{
  DisplayMode = EditStack[--EditSPtr];
  EndHook = (void *)EditStack[--EditSPtr];
  HomeHook = (void *)EditStack[--EditSPtr];
  EditYOff = EditStack[--EditSPtr];
  EditXOff = EditStack[--EditSPtr];
  ActEditWindow = (void *)EditStack[--EditSPtr];
  LineWOff = EditStack[--EditSPtr];
  LineWSize = EditStack[--EditSPtr];
  LineBSize = EditStack[--EditSPtr];
  LineBPos = EditStack[--EditSPtr];
  LineBLen = EditStack[--EditSPtr];
  EditBuf = (void *)EditStack[--EditSPtr];
  PgDownHook = (void *)EditStack[--EditSPtr];
  PgUpHook = (void *)EditStack[--EditSPtr];
  DownHook = (void *)EditStack[--EditSPtr];
  UpHook = (void *)EditStack[--EditSPtr];
  EnterHook = (void *)EditStack[--EditSPtr];
  TabHook = (void *)EditStack[--EditSPtr];
  CharHook = (void *)EditStack[--EditSPtr];

  if (EditSPtr == 0) DisplayMode &= 65535-OTHERWINDOW;
}

void WriteLn(FILE *Fl, char *Buf)
{
  fprintf(Fl, "%s\n", Buf);
}

void UpdateOptions()
{
  FILE *Fl;
  int i, n;

  /*** Set color names ***/
  /* Find first color option */
  for(i=0;strcasecmp(OptFmt[i].ID, "TERMFG");i++);

  for(n=0;n<18;n++) OptFmt[n+i].Buf = ColorMenu[(n&1?BGs[(n>>1)]:FGs[(n>>1)])+1].Text;

  if (*Profile)
    sprintf(MultiBuf, "%s/.ewterm.options.%s", getenv("HOME"), Profile);
  else
    sprintf(MultiBuf, "%s/.ewterm.options", getenv("HOME"));
  Fl = fopen(MultiBuf, "wt");
  if (Fl) {
    WriteLn(Fl, OPTID);
    for(i=0;OptFmt[i].ID[0];i++) fprintf(Fl, "%s=%s\n", OptFmt[i].ID, OptFmt[i].Buf);

    fclose(Fl);
  }
  else {
    sprintf(MultiBuf, "** Could not save preferences: %s !\n", strerror(errno));
    AddEStr(MultiBuf, 0, 0);
    beep();
    wnoutrefresh(CUAWindow);
    redrawwin(OptWindow);
  }
}

void PrintWarning()
{
  AddEStr("** Warning: The change you made will take effect\n", 0, 0);
  AddEStr("** on the next start of EWTerm, not immediately.\n\n", 0, 0);
  beep();
  wnoutrefresh(CUAWindow);
  redrawwin(OptWindow);
}

void DoSetHost()
{
  /* XXX: This is reused by other GetString()s as well! --pasky */
  UpdateOptions();

  PrintWarning();
}

void StartSetHost()
{
  GetString(HostName, 256, "Hostname", DoSetHost, 0);
}

void DoSetPort()
{
  if (*HostPortStr) {
    HostPort = atoi(HostPortStr);
  }

  UpdateOptions();

  PrintWarning();
}

void StartSetPort()
{
  GetString(HostPortStr, 9, "Host port", DoSetPort, 0);
}

void DoSetBurstLines()
{
  if (*BurstLinesStr) {
    BurstLines = atoi(BurstLinesStr);
  } else {
    BurstLines = -1;
  }

  UpdateOptions();

  PrintWarning();
}

void StartSetBurstLines()
{
  GetString(BurstLinesStr, 9, "Burst lines (-1=unlimited)", DoSetBurstLines, 0);
}

void DoConnPassword()
{
  UpdateOptions();
  PrintWarning();
}

void ChangeConnPassword()
{
  DisplayMode |= HIDE;
  GetString(ConnPassword, 29, "Receiver Password", DoConnPassword, 0);
}

void ChangePassword3()
{
  unsigned char *DstPtr, *SrcPtr;
  int a, b, i;

  /* Encode password */
  ActPasswd[29] = 0;
  DstPtr = CodedPasswd;
  SrcPtr = ActPasswd;
  while(*SrcPtr) {
    a = *SrcPtr++ ^ 0xAA;
    for(i=0;i<2;i++) {
      b = a & 0xF;
      if (b >= 10) b += 7;
      *DstPtr++ = b+48;
      a >>= 4;
    }
  }
  *DstPtr = 0;

  UpdateOptions();
}

void ChangePassword()
{
  DisplayMode |= HIDE;
  GetString(ActPasswd, 29, "Password", ChangePassword3, 0);
}

void SetLFlFile()
{
  RedrawKeys();

  PrintWarning();
}

void SwitchLFL()
{
  FileRequest(LogFName, "Select log file", SetLFlFile);
    
  RedrawKeys();
}

/* Option menu routines */
void DoSetHistLimit()
{
  sscanf(CmdHistLenT, "%d", &CmdHistLen);
  UpdateOptions();
}

void SetHistLimit()
{
  GetString(CmdHistLenT, 30, "Store cmds longer than N chrs", DoSetHistLimit, 0);
}

void SetHelpFile()
{
  GetString(FormsDir, 256, "Directory to be searched for EWSD commands", UpdateOptions, 0);
}

void SetFilterCommand()
{
  GetString(FilterCmd, 50, "Filter to be ran upon the exchange output", DoSetHost, 0);
}

void UpdateHistoryMode()
{
  char *TmpPtr;

  TmpPtr = OptionList[5].Data;
  while((*TmpPtr) && (*TmpPtr !=':')) TmpPtr++;

  if (*TmpPtr) {
    TmpPtr+=2;
    if (HistoryMode[0] == 'B') strcpy(TmpPtr, "Bottom");
    else strcpy(TmpPtr, "Keep  ");
  }
  UpdateOptions();
}

void UpdateMonoMode()
{
  char *TmpPtr;

  TmpPtr = OptionList[8].Data;
  while((*TmpPtr) && (*TmpPtr !=':')) TmpPtr++;

  if (*TmpPtr) {
    TmpPtr+=2;
    if (ForceMono == '1') strcpy(TmpPtr, "Yes ");
    else strcpy(TmpPtr, "No  ");
  }
  UpdateOptions();
}

void ChangeHistoryMode()
{
  if (HistoryMode[0] == 'B') HistoryMode[0] = 'K';
  else HistoryMode[0] = 'B';
  UpdateHistoryMode();
  ListDrawAct();
  UpdateOptions();
}

void ChangeForceMonoMode()
{
  if (ForceMono == '1') ForceMono = '0';
  else ForceMono = '1';
  UpdateMonoMode();
  ListDrawAct();
  UpdateOptions();
}


void HideOptions()
{
  PopEditOptions();  /* Pop pushed by list */
  del_panel(OptPanel);
  delwin(OptWindow);
  update_panels();
  SetMenu(0, 0);
}
void OptChoosed()
{
  ListDrawAct();
  OptPtrs[ListActNum]();
}


void ShowOptions()
{
  NewWindow(22, 14, "More options", &OptWindow, &OptPanel);
  if (OptWindow) {
    SetMenu(OptionsMenu, 1);
    RedrawKeys();

    UpdateHistoryMode();

    InitList(OptWindow, 1, 1, 22, 14, (void *)OptionList, 0, 0, CMP_NONE);
    ListControl(NULL, OptChoosed);
  }  
}

void ReadLine(FILE *Fl)
{
  char a, *TmpPtr;

  TmpPtr = MultiBuf;
  do {
    a = fgetc(Fl);
    if (a == -1) break;
    if ((a == 13) || (a == 10)) continue;
    *TmpPtr++ = a;
  } while((a != 10) && (!feof(Fl)));
  *TmpPtr = 0;
}

void ReadOptLine(FILE *Fl)
{
  char *TmpPtr;
  int n, i, ci;

  ReadLine(Fl);
 
  TmpPtr = strchr(MultiBuf, '=');
  if (TmpPtr == 0) return;
  *(TmpPtr++) = 0;

  ci = 0;
  for(i=0;OptFmt[i].ID[0] != 0;i++) {
    if (strcasecmp(OptFmt[i].ID, MultiBuf) == 0) {

      if (OptFmt[i].Buf == 0) {  /* Read color */
	/* Find color */
	for(n=1;n<9;n++)
	  if (strcasecmp(TmpPtr, ColorMenu[n].Text) == 0) break;

	if (n == 9) n=1;
	n--;

	if (ci & 1) BGs[ci>>1] = n;
	else FGs[ci>>1] = n;
      }
      else {
	strncpy(OptFmt[i].Buf, TmpPtr, OptFmt[i].Len);
	OptFmt[i].Buf[OptFmt[i].Len] = 0;
      }
      
      return;
    }
    if (OptFmt[i].Buf == 0) ci++;  /* Increase color index */
  }
}

void ReadOptions(char *FileName)
{
  FILE *Fl;

  Fl = fopen(FileName, "rt");

  getcwd(MultiBuf, 256);

  if (Fl) {
    ReadLine(Fl);
    if (strcasecmp(MultiBuf, OPTID) > 1) BadOpt = 1;
    else {
      while(!feof(Fl)) ReadOptLine(Fl);      
    }
    fclose(Fl);

    sscanf(CmdHistLenT, "%d", &CmdHistLen);
  }
}

void SwitchAutoLog()
{
  AutoLogOn = AutoLogOn == '0' ? '1' : '0';

  if (CUAWindow) RedrawStatus();
  UpdateOptions();
}

void SwitchCaps(struct MenuEntry *TmpMenu)
{
  ConvertMode ^= CV2CAPS;

  if (ConvertMode & CV2CAPS) TmpMenu->Text = "NoCaps";
  else TmpMenu->Text = "Caps";

  RedrawKeys();
  if (CUAWindow) RedrawStatus();
}

void SwitchSemi(struct MenuEntry *TmpMenu)
{
  ConvertMode ^= ADDSEMI;

  if (ConvertMode & ADDSEMI) TmpMenu->Text = "NoSemi";
  else TmpMenu->Text = "Semi";

  RedrawKeys();
  if (CUAWindow) RedrawStatus();
}
