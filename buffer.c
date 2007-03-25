/* buffer.c - buffer manipulation */

#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "ewterm.h"

#define MAXNUMBUF 60000	/* Max # of lines in scrollback */

WINDOW *BufWindow;
PANEL *BufPanel;

char ActLBuf[256];
int ActLBPos;		/* # of characters in ActLBuf */
unsigned char ContType;	/* Is there a command in the buffer */

struct THistE *LastBuf, *FirstBuf;
struct THistE *TopBuf;		/* Ptr to top line of buffer screen */
struct THistE *LastFound;	/* Last found line */
struct THistE *CUATop;		/* Top line visible in CUA Window */

int NumBuf;			/* # of records in buffer */
char SearchBuf[51];
int MaxLineNum;
char BufSaveFName[256];
char BufferSavePath[260];

void BufSave(), BufFindNext(), BufFind(), BufClear(), StopShowBuf();

struct MenuEntry BufMenu[10] = {
  {"Help", 0, ShowHelp},
  {"Save", 0, BufSave},
  {"Find Next", 0, BufFindNext},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"Find", 0, BufFind},
  {"Clear", 0, BufClear},
  {"", 0, NULL},
  {"Back", 0, StopShowBuf}
};



char *strcasestr(char *haystack, char *needle)
{
  char *Ptr1, *Ptr2;
  int Len = strlen(needle);

  if (needle[0] == 0) return(haystack);

  for(;;) {
    Ptr1 = index(haystack, needle[0]);
    if (isalpha(needle[0])) {
      Ptr2 = index(haystack, needle[0]^32);
      if (Ptr2 != 0) 
       if ((Ptr1 == 0) || (Ptr2 < Ptr1)) Ptr1 = Ptr2;
    }

    if (Ptr1 == 0) return(0);

    haystack = Ptr1;
    if (strncasecmp(haystack, needle, Len) == 0) return(Ptr1);
    haystack++;
  }
}

/* Puts history (buffer) to screen from given position */
void DrawHistory(struct THistE *TmpEntry)
{
  int i, y, x;

  for(i=0;(i<CUALines) && (TmpEntry != 0);i++) {

    if (UsingColor) 
      switch(TmpEntry->Color) {
      case COL_CMD:
	wattron(CUAWindow, ATT_CMD);
	SetBright(CUAWindow, COL_CMD);
	break;
      case COL_PROMPT:
	wattron(CUAWindow, ATT_PROMPT);
	SetBright(CUAWindow, COL_PROMPT);
	break;
      case COL_ERR:
	wattron(CUAWindow, ATT_ERR);
	SetBright(CUAWindow, COL_ERR);
	break;
      case COL_TERM:
	wattron(CUAWindow, ATT_TERM);
	SetBright(CUAWindow, COL_TERM);
	break;
      }
    
    strncpy(MultiBuf, TmpEntry->Data, COLS);
    MultiBuf[COLS] = 0;
    if ((strlen(MultiBuf) == COLS) && (MultiBuf[COLS-1] != 10)) {
      waddstr(CUAWindow, MultiBuf);
      getyx(CUAWindow, y, x);
      wmove(CUAWindow, y+1, 0);
    }
    else {
      waddstr(CUAWindow, MultiBuf);
      if (((BGs[COL_TERM] != 0) || (BGs[COL_CMD] != 0) || (BGs[COL_PROMPT] != 0) || (BGs[COL_ERR])) && (UsingColor)) {
	wmove(CUAWindow, i, 0);
	switch(TmpEntry->Color) {
	case COL_TERM:
          FillLine(CUAWindow, COL_TERM);
	  break;
	case COL_CMD:
          FillLine(CUAWindow, COL_CMD);
	  break;
	case COL_PROMPT:
          FillLine(CUAWindow, COL_PROMPT);
	  break;
	case COL_ERR:
          FillLine(CUAWindow, COL_ERR);
	  break;
	}
	wmove(CUAWindow, i+1, 0);
      }
    }

    TmpEntry = TmpEntry->Next;

    if (UsingColor) {
      wattron(CUAWindow, ATT_TERM);
      SetBright(CUAWindow, COL_TERM);
    }

  }
  if (i < CUALines) {
    /* Draw last line */
    ActLBuf[ActLBPos] = 0;
    waddstr(CUAWindow, ActLBuf);
  }
  UpdateCUAW = 1;
}

void RedrawCUAWindow()
{
  wmove(CUAWindow, 0, 0);
  if (CUATop) DrawHistory(CUATop);
}


void AddLineToBuffer(char Type, char Buffer)
{
  struct THistE *TmpEntry;
  struct THistE **LastEntry, **FirstEntry;
  int Size;
  char *Data;
  int *Counter, Max;

  switch(Buffer) {
    case LINE:
      ActLBuf[ActLBPos++] = 0;
      LastEntry = &LastBuf;
      FirstEntry = &FirstBuf;
      Size = sizeof(*TmpEntry)+ActLBPos+4;
      Data = ActLBuf;
      Counter = &NumBuf;
      Max = MAXNUMBUF;

      /* Reset buffer */
      ActLBPos = 0;
      ContType = LINE;
      break;
      
    case COMMAND:
      LastEntry = &LastLine;
      FirstEntry = &FirstLine;
      Size = sizeof(*TmpEntry)+LineBLen+4;
      Data = LineBuf;
      Counter = &NumHist;
      Max = MAXNUMHIST;
      break;
    default:
      AddEStr("*** Internal error: Unknown buffer type !\n", 0, 0);
  }

  /* Create entry */
  TmpEntry = (void *)malloc(Size);
  strcpy(TmpEntry->Data, Data);
  TmpEntry->Type = Type;
  TmpEntry->Color = ActCol;  /* Has no meaning for COMMAND */
  if (Buffer == LINE) {
    TmpEntry->Number = MaxLineNum++;	/* Number lines in scrollback */
    /* !!! Possibly do renumbering here if MaxLineNum == 0 !!! */
  }
  else TmpEntry->Number = NumHist;

  /* Add record to the list */
  if (*LastEntry != 0) {
    (*LastEntry)->Next = TmpEntry;
    TmpEntry->Prev = *LastEntry;
    TmpEntry->Next = NULL;
    *LastEntry = TmpEntry;
  }
  else {	/* Add first entry */
    TmpEntry->Next = NULL;
    TmpEntry->Prev = NULL;
    *LastEntry = TmpEntry;
    *FirstEntry = TmpEntry;
  }
  (*Counter)++;

  /* Test # of records and free one if necessary */
  if (*Counter > Max) {    
    if ((Buffer == LINE) && (TopBuf == *FirstEntry)) {
      /* Advance history buffer top position if first shown line
         will be freed */
      TopBuf = TopBuf->Next;
    }

    TmpEntry = (*FirstEntry)->Next;
    free(*FirstEntry);
    TmpEntry->Prev = NULL;
    *FirstEntry = TmpEntry;
    (*Counter)--;
  }
  pdebug("AddLineToBuffer end\n");
}

/* Puts char to main window */
void AddCh(char Chr)
{
  int y, x;
  int Tmp;

  UpdateCUAW = 1;
  if (Chr != 10 && Chr != 13) waddch(CUAWindow, Chr);

  switch(Chr) {
    case 10:
      ActLBuf[ActLBPos++] = 10;
      AddLineToBuffer(ContType, LINE);
      Tmp = ActLBPos;

      if (CUATop == 0) CUATop = FirstBuf;	/* Set top CUA line */
      getyx(CUAWindow, y, x);

      if (ActLBPos < COLS) waddch(CUAWindow, 10);

      /* Set color to the end */
      if (((BGs[COL_TERM] != 0) || (BGs[COL_CMD] != 0) || (BGs[COL_PROMPT] != 0) || (BGs[COL_ERR])) && (UsingColor)) {
	wmove(CUAWindow, y, 0);
	switch(ActCol) {
	case COL_TERM:
          FillLine(CUAWindow, COL_TERM);
	  break;
	case COL_ERR:
          FillLine(CUAWindow, COL_ERR);
	  break;
	case COL_CMD:
          FillLine(CUAWindow, COL_CMD);
	  break;
	case COL_PROMPT:
          FillLine(CUAWindow, COL_PROMPT);
	  break;
	}
	wmove(CUAWindow, y+1, 0);
      }

      if (ActCol != COL_PROMPT) {
	/* If it's prompt, we'll reset it ourselves in exch.c. */
	ActCol = COL_TERM;
	if (UsingColor) {
	  wattron(CUAWindow, ATT_TERM);  /* Reset color */
	  SetBright(CUAWindow, COL_TERM);
	}
      }

      if (!(DisplayMode & OTHERWINDOW)) wnoutrefresh(CUAWindow);
      break;
    case 13:
      break;	/* Just ignore */
    case 8:	/* BSpc       */
      /* Clear character */
      waddch(CUAWindow, ' ');
      waddch(CUAWindow, Chr);
      if (ActLBPos) ActLBPos--;
      break;
    default:
      ActLBuf[ActLBPos++] = Chr;
      if (ActLBPos == (COLS+1)) {
        ActLBPos--;
        ActLBuf[COLS] = 0;
        AddLineToBuffer(ContType, LINE);

        if (CUATop == 0) CUATop = FirstBuf;	/* Set top CUA line */
        getyx(CUAWindow, y, x);

        ActLBPos = 1;
        ActLBuf[0] = Chr;
      }
  }
}

/* Prints string with ERROR colors */
void AddEStr(char *Str, char Convert, char PutToSerial)
{
  int x, y;

  getyx(CUAWindow, y, x);
  if (x != 0) AddStr("\n", 0, 0);

  ActCol = COL_ERR;
  if (!InBurst) beep();
  if (UsingColor) {
    wattron(CUAWindow, ATT_ERR);
    SetBright(CUAWindow, COL_ERR);
  }
  AddStr(Str, Convert, PutToSerial);
}

/* Puts string to main window */
void AddStr(char *Str, char Convert, char PutToSerial)
{
  char *TmpPtr, *TmpPtr2;
  char a, LastCh;

  TmpPtr = Str;
  LastCh = 0;
  if (Convert) {
    TmpPtr2 = MultiBuf;
    do {
      a = *TmpPtr++;
//      if (a == 13) continue;
      if (ConvertMode & CV2CAPS) a = toupper(a);
      if (ConvertMode & ADDSEMI) {
       if ((!a || (a == 10) || (a == 13)) && (LastCh != ';')) *(TmpPtr2++) = ';';
      }

      *(TmpPtr2++) = a;
      LastCh = a;
    } while(a);

    TmpPtr = MultiBuf;
  }

  if (PutToSerial && connection) {
    while(*TmpPtr)
      ESendChar(*(TmpPtr++));
  } else
    while(*TmpPtr)
      AddCh(*(TmpPtr++));
}

void RedrawHistoryBuf()
{
  struct THistE *TmpBuf;
  int i, y, x;

  /* Clear window */
  werase(BufWindow);
  if ((BGs[2] != 0) && (UsingColor)) {
    for(i=0;i<CUALines;i++) {
      wmove(BufWindow, i, 0);
      FillLine(BufWindow, COL_BUF);
    }
    wmove(BufWindow, 0, 0);
  }
  scrollok(BufWindow, FALSE);

  TmpBuf = TopBuf;
  for(i=0;(i<CUALines)&&(TmpBuf);i++) {

    if (UsingColor)
      switch(TmpBuf->Color) {
/*      case COL_CMD:
	wattron(BufWindow, ATT_CMD);
	SetBright(BufWindow, COL_CMD);
	break;
      case COL_ERR:
	wattron(BufWindow, ATT_ERR);
	SetBright(BufWindow, COL_ERR);
	break;
      case COL_TERM:*/
      default:
	wattron(BufWindow, ATT_BUF);
	SetBright(BufWindow, COL_BUF);
	break;
      }

    strncpy(MultiBuf, TmpBuf->Data, COLS);
    MultiBuf[COLS] = 0;
    if ((strlen(MultiBuf) == COLS) && (MultiBuf[COLS-1] != 10)) {
      waddstr(BufWindow, MultiBuf);
      getyx(BufWindow, y, x);
      wmove(BufWindow, y+1, 0);
    }
    else {
      waddstr(BufWindow, MultiBuf);
      if (((BGs[COL_BUF] != 0) || (BGs[COL_CMD] != 0) || (BGs[COL_PROMPT] != 0) || (BGs[COL_ERR])) && (UsingColor)) {
	wmove(BufWindow, i, 0);
	switch(TmpBuf->Color) {
		/*
	case COL_ERR:
          FillLine(BufWindow, COL_ERR);
	  break;
	case COL_CMD:
          FillLine(BufWindow, COL_CMD);
	  break;
	case COL_TERM:*/
	default:
          FillLine(BufWindow, COL_BUF);
	  break;
	}
	wmove(BufWindow, i+1, 0);
      }
    }
    TmpBuf = TmpBuf->Next;

    if (UsingColor) {
      wattron(BufWindow, ATT_BUF);
      SetBright(BufWindow, COL_BUF);
    }
  }
  wnoutrefresh(BufWindow);
}


/* Hooks */


void BufferUp()
{
  if (TopBuf)
   if (TopBuf->Prev) {
     TopBuf = TopBuf->Prev;
     RedrawHistoryBuf();
   }
}

void BufferDown()
{
  if (TopBuf)
   if (TopBuf->Next) {
     TopBuf = TopBuf->Next;
     RedrawHistoryBuf();
   }
}

void BufferPgUp()
{
  int i;

  if (TopBuf)
   if (TopBuf->Prev) {
     for(i=2;(i<CUALines) && (TopBuf->Prev);i++) TopBuf = TopBuf->Prev;
     RedrawHistoryBuf();
   }
}

void BufferPgDown()
{
  int i;

  if (TopBuf)
   if (TopBuf->Next) {
     for(i=2;(i<CUALines) && (TopBuf->Next);i++) TopBuf = TopBuf->Next;
     RedrawHistoryBuf();
   }
}

void BufHome()
{
  TopBuf = FirstBuf;
  RedrawHistoryBuf();
}

void BufEnd()
{
  TopBuf = LastBuf;
  RedrawHistoryBuf();
}

/* When user presses ENTER in scrollback, we will close scrollback and send ENTER. */
void BufEnterHook()
{
  StopShowBuf();
  EnterHook();
}



/* LastFound is set to position at which should the search begin */
void BufDoFind()
{
  if (SearchBuf[0] == 0) return;		/* Don't search for empty string */

  if (LastFound == 0) LastFound = FirstBuf;	/* We're just starting */
  else {
    if (LastFound->Number < FirstBuf->Number) {
      /* Line with last found apperance has been freed, start from the first one */
      LastFound = FirstBuf;
    }
  }

  while(LastFound) {
    if (strcasestr(LastFound->Data, SearchBuf)) break;
    LastFound = LastFound->Next;
  }

  if (LastFound == 0) beep();
  else {
    TopBuf = LastFound;
    RedrawHistoryBuf();
  }
}

void BufFindExec()
{
  LastFound = TopBuf;
  BufDoFind();
}

void BufFind()
{
  GetString(SearchBuf, 50, "Search for", BufFindExec, 0);
}

void BufFindNext()
{
  if (SearchBuf[0] == 0) BufFind();
  else {
    if ((LastFound == TopBuf)  && (TopBuf)) LastFound = TopBuf->Next;
    else LastFound = TopBuf;
    if (LastFound == 0) beep();
    else BufDoFind();
  }
}

void AppendSlash(char *Buf)
{
  if (*Buf == 0) {
    Buf[0] = '/';
    Buf[1] = 0;
  }

  while(*Buf) Buf++;
  if (*(Buf-1) != '/') {
    Buf[0] = '/';
    Buf[1] = 0;
  }
}

void BufDoSave()
{
  FILE *Fl;
  struct THistE *TmpHist;

  strcpy(BufferSavePath, FRqDir);
  AppendSlash(BufferSavePath);
  UpdateOptions();

  Fl = fopen(BufSaveFName, "w");
  if (Fl == 0) beep();

  TmpHist = FirstBuf;
  while(TmpHist) {
    fputs(TmpHist->Data, Fl);
//    if (TmpHist->Length >= COLS) fputc(10, Fl);
    TmpHist = TmpHist->Next;
  }

  fclose(Fl);
}

void BufSave()
{
  strcpy(BufSaveFName, BufferSavePath);
  FileRequest(BufSaveFName, "Save to", BufDoSave);
}

void BufDoClear()
{
  struct THistE *TmpPtr, *TmpPtr2;

  TmpPtr = FirstBuf;
  while(TmpPtr) {
    TmpPtr2 = TmpPtr->Next;
    free(TmpPtr);
    TmpPtr = TmpPtr2;
  }
  LastBuf = 0;
  FirstBuf = 0;
  NumBuf = 0;
  TopBuf = 0;
  LastFound = 0;
  CUATop = 0;
  wclear(CUAWindow);
  ActLBuf[ActLBPos] = 0;
  mvwaddstr(CUAWindow, 0, 0, ActLBuf);
  UpdateCUAW = 1;
  RedrawHistoryBuf();
}

void BufClear()
{
  AskBool("Clear", BufDoClear, NULL);
}



/* Starts history buffer display */
void StartShowBuf()
{
  int i;

  SetMenu(BufMenu, 1);

  NewWindow(COLS-2, CUALines-2, NULL, &BufWindow, &BufPanel);

  if (UsingColor) {
    wattroff(BufWindow, ATT_WIN);
    wattron(BufWindow, ATT_BUF);
    SetBright(BufWindow, COL_BUF);
  }
  top_panel(BufPanel);

  PushEditOptions();
  TabHook = Dummy;
  EnterHook = BufEnterHook;
  UpHook = BufferUp;
  DownHook = BufferDown;
  PgUpHook = BufferPgUp;
  PgDownHook = BufferPgDown;
  HomeHook = BufHome;
  EndHook = BufEnd;

  if (NumBuf < CUALines) TopBuf = FirstBuf;
  else {
    TopBuf = LastBuf;
    for(i=2;i<CUALines;i++) TopBuf = TopBuf->Prev;
  }

  DisplayMode |= BUFSHOWN;
  RedrawHistoryBuf();
/*  RedrawStatus(); */
}

void StopShowBuf()
{
  int LWO, LWS, LBS, LBP, LBL;

  SetMenu(0, 0);

  /* We have to keep some variables and not to pop them */
  LWO = LineWOff;
  LWS = LineWSize;
  LBS = LineBSize;
  LBP = LineBPos;
  LBL = LineBLen;
  PopEditOptions();

  /* Return values */
  LineWOff = LWO;
  LineWSize = LWS;
  LineBSize = LBS;
  LineBPos = LBP;
  LineBLen = LBL;

  if (BufPanel) del_panel(BufPanel);
  if (BufWindow) delwin(BufWindow);

  DisplayMode &= 65535-BUFSHOWN;
  wnoutrefresh(CUAWindow);

  if (CUAWindow) RedrawStatus();
}

