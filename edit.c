/* edit.c - Commandline buffer, edit functions */

#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include <ctype.h>

#include "ewterm.h"

WINDOW *ActEditWindow;

#define EDITBLEN 1024	/* Length of edit buf */

/* Line edit buffer */
char LineBuf[EDITBLEN];
char *EditBuf;

int LineBLen, LineBPos, LineBSize, LineWSize, LineWOff;
int EditYOff, EditXOff;



/* When line is changed, this routine is called. It checks whether
   recently editted line (before entering history) was stored. If so,
   removes it from the list. Also clears ActHist pointer, so when
   'UP ARROW' key is pressed, the last line in history will be recalled */
void LineChanged()
{
  struct THistE *TmpPtr;

  if ((ActHist != NULL) && (ClearLastLine)) {  /* We have work to do */ 
    /* Remove recently editted line from history */
    ClearLastLine = 0;
    TmpPtr = LastLine;
    LastLine = TmpPtr->Prev; /* Entry which will become the last */
    LastLine->Next = NULL;
    free(TmpPtr);
    NumHist--;
  }
  ActHist = NULL;	/* Clear last history recalled */
}

/* Refreshes edit line */
void RefreshEdit()
{
  int X;
  char *TmpPtr;

  X = EditXOff;
  TmpPtr = EditBuf+LineWOff;
  if (DisplayMode & HIDE) while((*TmpPtr) && (X < (LineWSize+EditXOff))) mvwaddch(ActEditWindow, EditYOff, X++, (TmpPtr++, '*')); 
  else while((*TmpPtr) && (X < (LineWSize+EditXOff))) mvwaddch(ActEditWindow, EditYOff, X++, *(TmpPtr++));
  while(X < (LineWSize+EditXOff)) mvwaddch(ActEditWindow, EditYOff, X++, ' ');
}

/* Inserts character to an edit buffer */
void InsertChr(char Chr)
{
  int TmpI;

  LineChanged();
  if ((ConvertMode & OVERWRITE) && (LineBPos < LineBLen)) {
    EditBuf[LineBPos] = Chr;
    LineBPos++;
  }
  else {
    if (LineBLen < LineBSize) {
      TmpI = LineBLen;
      while(TmpI >= LineBPos) {
        EditBuf[TmpI+1] = EditBuf[TmpI];
        TmpI--;
      }
      EditBuf[TmpI+1] = Chr;
      LineBLen++;
      LineBPos++;
      if ((LineBPos-LineWOff) >= LineWSize) LineWOff++;
    }
    else beep();
  }
}

/* Removes char from buf at given position */
void RemoveChar(int Pos)
{
  do {
    EditBuf[Pos] = EditBuf[Pos+1];
    Pos++;
  } while(EditBuf[Pos]);
  LineBLen--;
}

void EditHome()
{
  LineWOff = 0;
  LineBPos = 0;
  RefreshEdit();
}

void EditEnd()
{
  LineWOff = LineBLen-LineWSize+1;
  if (LineWOff < 0) LineWOff = 0;
  LineBPos = LineBLen;
  RefreshEdit();
}

void DefPgUpHook()
{
  StartShowBuf();
  PgUpHook();
}

void CmdEnterHook()
{
  char Add;
  struct THistE *Ptr;

  DontGoUp = 0;

  OldActHist = (HistoryMode[0]=='B')?0:ActHist;
  if (OldActHist == 0) Add = 1;

  Ptr = ActHist;
  LineChanged();

  if ((((OldActHist == 0) && ((LastLine != Ptr) || (LastLine == 0))) ||
       ((LastLine == ActHist) && (ClearLastLine)))
       && (strlen(LineBuf) > CmdHistLen))
    AddLineToBuffer(0, COMMAND);

  ActHist = 0;

  if (OldActHist && (HistoryMode[0] != 'B')) DontGoUp = 1;  /* Just display actual history entry */

  LineBPos = LineBLen;
  InsertChr(13);
  /* Flush line */
  ContType = COMMAND;
  AddCommandToQueue(LineBuf, 1);
  
  LineBPos = 0;
  LineBLen = 0;
  LineWOff = 0;
  LineBuf[0] = 0;
  RefreshEdit();
}

/* Hook for 'command line' editing */
void BufHook(int Res)
{
  if ((Res < 256) && (IsNonModal)) return;

  if (Res == 13) Res = 10;
  switch(Res) {
    case 9:
      TabHook();
      break;
    case 10:
      EnterHook();
      break;
    case 12: /* ^L */
      UpdateCUAW = 1;
      redrawwin(stdscr);
      break;
    case KEY_BACKSPACE:
    case 127:
    case 8:
      ActKey = 8;
      if (CharHook != 0) CharHook();
      else if ((DisplayMode & NOEDIT) == 0) {
        if (LineBPos > 0) {
          LineChanged();
          RemoveChar(LineBPos-1);
          LineBPos--;
          if (LineBPos < LineWOff) LineWOff--;
          RefreshEdit();
        }
      }
      break;
    case KEY_DC:
    case 4:		/* CTRL-D */
      if ((DisplayMode & NOEDIT) == 0) {
        if (LineBPos < LineBLen) {
          LineChanged();
          RemoveChar(LineBPos);
          RefreshEdit();
        }
      }
      break;
    case KEY_LEFT:
    case 2:		/* CTRL-B */
      if (LineBPos != 0) LineBPos--;
      if (LineBPos < LineWOff) {
        LineWOff--;
        RefreshEdit();
      }
      break;
    case KEY_RIGHT:
    case 6:		/* CTRL-F */
      if (LineBPos < LineBLen) LineBPos++;
      if ((LineBPos-LineWOff) >= LineWSize) {
        LineWOff++;
        RefreshEdit();
      }
      break;
    case KEY_UP:
    case 16:            /* CTRL-P */
      UpHook();
      break;
    case KEY_DOWN:
    case 14:            /* CTRL-N */
      DownHook();
      break;
    case KEY_HOME:
    case 1:		/* CTRL-A */
      HomeHook();
      break;
    case KEY_END:
    case 5:		/* CTRL-E */
      EndHook();
      break;
    case KEY_PPAGE:
    case 20:            /* CTRL-T */
      PgUpHook();
      break;
    case KEY_NPAGE:
    case 22:            /* CTRL-V */
      PgDownHook();
      break;
    default:
      if (Res >= 256) ProcessSpecialKey(Res);
      else {
	ActKey = Res;
	if (CharHook != 0) CharHook();
        else if ((DisplayMode & NOEDIT) == 0) {
          InsertChr(Res);
          RefreshEdit();
        }
      }
  }
}


void DeleteWord()
{
  char Flg;

  if (DisplayMode & NOEDIT) return;

  /* Backspace word */
  Flg = 0;
  while (LineBPos > 0) {
    LineChanged();
    if (isalnum(EditBuf[LineBPos-1])) Flg = 1;
    else if (Flg == 1) break;
    RemoveChar(LineBPos-1);
    LineBPos--;
    if (LineBPos < LineWOff) LineWOff--;
  }
  RefreshEdit();
}

void ForwardWord()
{
  char Flg;

  if (DisplayMode & NOEDIT) return;

  /* Forward word */
  Flg = 0;
  while (LineBPos < LineBLen) {
    LineChanged();
    if (isalnum(EditBuf[LineBPos])) Flg = 1;
    else if (Flg == 1) break;
    LineBPos++;
    if ((LineBPos-LineWOff) >= LineWSize) LineWOff++;
  }
  RefreshEdit();
}

void BackwardWord()
{
  char Flg;

  if (DisplayMode & NOEDIT) return;

  /* Backward word */
  Flg = 0;
  while (LineBPos > 0) {
    LineChanged();
    if (isalnum(EditBuf[LineBPos-1])) Flg = 1;
    else if (Flg == 1) break;
    LineBPos--;
    if (LineBPos < LineWOff) LineWOff--;
  }
  RefreshEdit();
}

/* Process key got after the ESC code
   Returns key simulated to be pressed on NULL
*/
int ProcessEscKey(int Key)
{
  if ((Key >= '0') && (Key <= '9')) return(KEY_F( (Key=='0')?10:(Key-'0') ));
  if (Key == 27) return(KEY_F(10));
  if (IsNonModal) return(0);

  switch(toupper(Key)) {
    case 127:
    case 8:
      DeleteWord();
      break;
    case 'F':
      ForwardWord();
      break;
    case 'B':
      BackwardWord();
      break;
    case 'X':
      AskQuit();
      break;
    case 'R':
      if (!((DisplayMode & OTHERWINDOW) || IsNonModal || (DisplayMode & NOEDIT) || (FirstLine == 0)))
	ShowHistoryMenu();
      else
	beep();
      break;
  }
  return(0);
}

/* Processes hotkeys */
void ProcessSpecialKey(int Key)
{
  struct MenuEntry *TmpEntry;
  void (*Func)();

  if ((Key >= KEY_F(1)) && (Key <= KEY_F(10))) {

    TmpEntry = &ActMenu[Key-KEY_F(1)];
    if (TmpEntry->Type == 0) {
      if (TmpEntry->Addr) {
        /* Call function */
        Func = TmpEntry->Addr;
        Func(TmpEntry);
      }
    }
    else SetMenu(TmpEntry->Addr, TmpEntry->Type == 1);
  }
  else {
     switch(Key) {
       case KEY_IC:
         ConvertMode ^= OVERWRITE;
         if (CUAWindow) RedrawStatus();
         break;
    }
  }
}

