/* history.c - Command history */

#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include <errno.h>
#include <unistd.h>

#include "ewterm.h"

WINDOW *HistWin;
PANEL *HistPanel;

struct THistE *LastLine = NULL, *FirstLine = NULL;
int NumHist = 0;			/* # of records in history */

char HistoryMode[2]="B";

struct THistE *ActHist, *OldActHist = 0;
char ClearLastLine, DontGoUp = 0;

char CmdHistLenT[31] = "0";
int CmdHistLen;

void ShowHelp(), HistEnter(), HistFind(), HideHistoryMenu();

struct MenuEntry HistMenu[10] = {
  {"Help", 0, ShowHelp},
  {"Use", 0, HistEnter},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"Find", 0, HistFind},
  {"", 0, NULL},
  {"", 0, NULL},
  {"Back", 0, HideHistoryMenu}
};


/* Copies line from command history to edit buffer */
void CopyFromHistory()
{
  strcpy(LineBuf, ActHist->Data);

  LineBLen = strlen(LineBuf);
  EditEnd();
}


/* Recalls previous line */
void PrevHistoryCommand()
{
  struct THistE *TmpPtr;

  if (NumHist != 0) {
    if (ActHist == NULL) { /* Add editted line to history */
      ClearLastLine = 1;
      AddLineToBuffer(0, COMMAND);
      ActHist = LastLine;

      if (OldActHist != 0) {
        ActHist = OldActHist;
        OldActHist = 0;
      }
    }

    TmpPtr = ActHist;
    if (DontGoUp) DontGoUp = 0;
    else TmpPtr = TmpPtr->Prev;
    if (TmpPtr) {
      ActHist = TmpPtr;
      CopyFromHistory();
    }
  }
}

/* Recalls previous line */
void NextHistoryCommand()
{
  struct THistE *TmpPtr;

  DontGoUp = 0;

  if ((ActHist == 0) && (OldActHist != 0)) {
    ClearLastLine = 1;
    AddLineToBuffer(0, COMMAND);
    ActHist = LastLine;
  }

  if (OldActHist != 0) {
    ActHist = OldActHist;
    OldActHist = 0;
  }

  if (ActHist != NULL) {
    TmpPtr = ActHist->Next;
    if (TmpPtr) {
      ActHist = TmpPtr;
      CopyFromHistory();
    }
  }
}

void HideHistoryMenu()
{
  PopEditOptions();
  del_panel(HistPanel);
  delwin(HistWin);
  update_panels();
  SetMenu(0, 0);
}

void HistEnter()
{
  HideHistoryMenu();

  if (ActHist == NULL) { /* Add editted line to history */
    ClearLastLine = 1;
    AddLineToBuffer(0, COMMAND);
  }
  ActHist = (void *)ListAct;
  CopyFromHistory();
  RefreshEdit();
}

static char FindStr[256] = "";

extern int ListTopNum, ListH;
extern struct TListEntry *ListTop;

void DoHistFind()
{
  struct THistE *HistE;
  int n = ListActNum + 1;

  for (HistE = (void *)ListAct?(void*)ListAct->Next:NULL; HistE; HistE = HistE->Next, n++) {
    if (strcasestr(HistE->Data, FindStr)) {
      ListAct = (void *)HistE;
      ListActNum = n;
      if ((ListActNum-ListTopNum) >= ListH) {
	ListTop = ListAct;
	ListTopNum = ListActNum;
      }
      ListDrawAct();
      break;
    }
  }
}

void HistFind()
{
  GetString(FindStr, 256, "Find", DoHistFind, NULL);
}

void ShowHistoryMenu()
{
  char *Ptr1, *Ptr2;
  int Tmp;

  DontGoUp = 0;

  NewWindow(70, 20, "Command history", &HistWin, &HistPanel);
  if (HistWin) {
    SetMenu(HistMenu, 1);
    RedrawKeys();

    Ptr1 = &FirstLine->Type;
    Ptr2 = FirstLine->Data;

    if (OldActHist != 0) {
      ActHist = OldActHist;
      OldActHist = 0;
    }

    if (ActHist == 0) Tmp = NumHist;
    else Tmp = ActHist->Number;
    InitList(HistWin, 1, 1, 70, 20, (void *)FirstLine, Ptr2-Ptr1, Tmp, CMP_NONE);
    ListControl(NULL, HistEnter);
  }
  else beep();
}


char *
ProfileHistoryFilename()
{
  if (*Profile)
    snprintf(MultiBuf, 64, "%s/.ewterm.history.%s", getenv("HOME"), Profile);
  else
    snprintf(MultiBuf, 64, "%s/.ewterm.history", getenv("HOME"));
  return MultiBuf;
}

void
SaveHistory()
{
  FILE *Fl;

  if (!LastLine) return;

  Fl = fopen(ProfileHistoryFilename(), "wt");
  if (Fl) {
    struct THistE *ActLine = LastLine;
    int i;

    for (i = 0; i < 100; i++) {
      if (!ActLine->Prev) break;
      ActLine = ActLine->Prev;
    }
    for (i = 0; i < 100; i++) {
      fprintf(Fl, "%s\n", ActLine->Data);
      ActLine = ActLine->Next;
      if (!ActLine) break;
    }

    fclose(Fl);
  }
  else {
    sprintf(MultiBuf, "** Could not save command history: %s !\n", strerror(errno));
    AddEStr(MultiBuf, 0, 0);
    wrefresh(CUAWindow);
    beep(); sleep(1);
  }
}
	

void
LoadHistory()
{
  FILE *Fl;

  Fl = fopen(ProfileHistoryFilename(), "r");
  if (Fl) {
    char buf[1024];
    struct THistE *ActLine = NULL, *PrevLine;

    while (fgets(buf, 1024, Fl)) {
      PrevLine = ActLine;
      ActLine = malloc(sizeof(struct THistE) + strlen(buf) + 1);
      if (!FirstLine) FirstLine = ActLine;

      if (PrevLine) PrevLine->Next = ActLine;
      ActLine->Prev = PrevLine;
      ActLine->Next = NULL;

      ActLine->Type = COMMAND;
      ActLine->Color = COL_EDIT;
      ActLine->Number = NumHist++;

      buf[strlen(buf) - 1] = 0; /* chop() */
      strcpy(ActLine->Data, buf);
    }
    ActHist = LastLine = ActLine;

    fclose(Fl);
  }
#if 0
  else {
    sprintf(MultiBuf, "Warning: Could not load command history: %s\n", strerror(errno));
    AddEStr(MultiBuf, 0, 0);
    wrefresh(CUAWindow);
    beep(); sleep(1);
  }
#endif
}
