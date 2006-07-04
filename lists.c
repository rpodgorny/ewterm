/* lists.c - Lists generic handling */

#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include "ewterm.h"

struct TListEntry *ListTop, *ListAct;
int ListX, ListY, ListW, ListH, ListDataO;
int ListTopNum, ListActNum;
WINDOW *ListWindow;
void (*ListUsrTab)();
void (*ListUsrEnter)();
int LBPos;
char ListCmpMode;
char ListBuf[21];  /* Buffer when user */


void SetStrLen(char *Str, int Len)
{
  char *TmpPtr;

  strcpy(MultiBuf, Str);

  TmpPtr = MultiBuf;
  while((Len--) > 0) {
    if (*TmpPtr == 0) break;
    else TmpPtr++;
  }

  while((Len--) > 0) *(TmpPtr++) = ' ';
  *TmpPtr = 0;
}

void ListMakeActive(int Which)
{
  if (ListAct == 0) return;

  while(Which > ListActNum) {
    if (ListAct->Next == 0) return;
    ListAct = ListAct->Next;
    ListActNum++;
    if ((ListActNum-ListTopNum) >= ListH) {
      ListTop = ListTop->Next;
      ListTopNum++;
    }
  }
  while(Which < ListActNum) {
    if (ListAct->Prev == 0) return;
    ListAct = ListAct->Prev;
    ListActNum--;
    if (ListActNum < ListTopNum) {
      ListTopNum = ListActNum;
      ListTop = ListAct;
    }
  }
}

void ListRedraw()
{
  int y, i;
  struct TListEntry *TmpEntry;
  char *TmpPtr;

  TmpEntry = ListTop;
  for(y=ListY;(y<(ListY+ListH)) && (TmpEntry != 0);y++) {
    SetStrLen(TmpEntry->Data+ListDataO, ListW+1);
    mvwaddstr(ListWindow, y, ListX, MultiBuf);
    TmpEntry = TmpEntry->Next;
  }

  TmpPtr = MultiBuf;
  for(i=0;i<ListW;i++) *TmpPtr++ = ' ';
  *TmpPtr = 0;

  while(y < (ListY+ListH)) mvwaddstr(ListWindow, y++, ListX, MultiBuf);

  wnoutrefresh(ListWindow);
}

void InitList(WINDOW *Win, int x, int y, int w, int h, struct TListEntry *First, int DataOffset, int Active, char CmpMode)
{
  ListWindow = Win;

  ListX = x;
  ListY = y;
  ListW = w;
  ListH = h;
  ListTop = First;
  ListAct = First;
  ListCmpMode = CmpMode;
  ListTopNum = 0;
  ListActNum = 0;
  ListDataO = DataOffset;
  ListMakeActive(Active);
  ListRedraw();
}

void ListEnter()
{
  if (ListAct != 0) {
    ListRedraw();	/* Deactivate */
    ListUsrEnter();
  }
}

void ListDrawAct()
{
  int y;

  y = ListY+(ListActNum-ListTopNum);
  EditYOff = y;
  EditXOff = ListX;
  LineBPos = 0;
  if (ListAct == 0) return;
  ListRedraw();
  wattron(ListWindow, A_REVERSE);

  SetStrLen(&ListAct->Data[ListDataO], ListW+1);
  mvwaddstr(ListWindow, y, ListX, MultiBuf);

  wattroff(ListWindow, A_REVERSE);
  wnoutrefresh(ListWindow);
}

void ListMove(int Offset)
{
  ListMakeActive(ListActNum+Offset);
  ListDrawAct();
}

void ListTab()
{
  if (ListUsrTab) {
    ListRedraw();	/* Deactivate */
    ListUsrTab();
  }
}

void ListUp()
{
  ListMove(-1);
}

void ListDown()
{
  ListMove(1);
}

void ListPgUp()
{
  ListMove(-(ListH-2));
}

void ListPgDown()
{
  ListMove(ListH-2);
}

void ListHome()
{
  ListMakeActive(0);
  ListDrawAct();
}

void ListEnd()
{
  ListMakeActive(65535);
  ListDrawAct();
}

/* Does either case sensitive or insensitive comparsion, depending on set mode */
int DoCmp(char *S1, char *S2)
{
  switch(ListCmpMode) {
  case CMP_CASE:
    return(strcmp(S1, S2));
  case CMP_NOCASE:
    return(strcasecmp(S1, S2));
  default:
    return(0);   /* Other */
  }
}

void ListChar()
{
  int Res;

  if (ListAct == 0) return;

  if (ActKey == 8) {
    if (LBPos > 0) LBPos--;  
  }
  else {
    if (LBPos < 20) ListBuf[LBPos++] = ActKey;
    else {
      beep();
      return;
    }
  }
  ListBuf[LBPos] = 0;

  /* Now find best matching entry */
  Res = DoCmp(ListBuf, ListAct->Data);
  if (Res < 0) { /* Go back */
    while(ListAct->Prev?(DoCmp(ListBuf, ListAct->Prev->Data) < 0):0) {
      ListMakeActive(ListActNum-1);
    }
  }
  else if (Res > 0) {  /* Go forth */
    while(ListAct->Next && (DoCmp(ListBuf, ListAct->Data) > 0)) {
      ListMakeActive(ListActNum+1);
    }
  }

  ListRedraw();
  ListDrawAct();
}

void ListControl(void (*TabRout)(), void (*EnterRout)())
{
  PushEditOptions();
  DisplayMode |= OTHERWINDOW;

  ListUsrTab = TabRout;
  ListUsrEnter = EnterRout;
  ActEditWindow = ListWindow;

  TabHook = ListTab;
  EnterHook = ListEnter;
  UpHook = ListUp;
  DownHook = ListDown;
  PgUpHook = ListPgUp;
  PgDownHook = ListPgDown;
  HomeHook = ListHome;
  EndHook = ListEnd;
  if (ListCmpMode != CMP_NONE) CharHook = ListChar;  
  LBPos = 0;

  DisplayMode |= NOEDIT;
  ListDrawAct();
}

