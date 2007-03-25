/* gstr.c - GetString functions */

#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include "ewterm.h"

WINDOW *GStrWindow;
PANEL *GStrPanel;
void (*GStrUsrEnd)(), (*GStrUsrCancel)();
char *GStrOrigBuf;
char GStrBuf[256];

void GStrOK(), GStrClear(), GStrUndo(), GStrCancel();

struct MenuEntry GStrMenu[10] = {
  {"OK", 0, GStrOK},
  {"Clear", 0, GStrClear},
  {"Undo", 0, GStrUndo},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"Cancel", 0, GStrCancel} 
};

/* Pops up a window awaiting string from user */
void GetStringDone(int WasOK)
{
  del_panel(GStrPanel);
  delwin(GStrWindow);
  PopEditOptions();
  DisplayMode &= 65535-HIDE-OTHERWINDOW;
  GStrPanel = 0;
  GStrWindow = 0;
  update_panels();
  SetMenu(0, 0);

  if (WasOK) {
    strcpy(GStrOrigBuf, GStrBuf);
    if (GStrUsrEnd) GStrUsrEnd();
  }
  else if (GStrUsrCancel) GStrUsrCancel();
}

void GStrCancel()
{
  GetStringDone(0);
}

void GStrOK()
{
  GetStringDone(1);
}

void GStrClear()
{
  GStrBuf[0] = 0;
  LineBLen = 0;
  LineBPos = 0;
  RefreshEdit();
}

void GStrUndo()
{
  strcpy(GStrBuf, GStrOrigBuf);
  LineBLen = strlen(EditBuf);
  LineBPos = LineBLen;
  RefreshEdit();
}

void GetString(char *UsrBuf, int BufLen, char *Title, void (*EndFunc)(), void (*UsrCancel)())
{
  int Wid;

  if (GStrPanel != 0) {
    beep();
    return;
  }

  Wid = BufLen+3;
  if (Wid > (MainW->_maxx-4)) Wid = MainW->_maxx-4;
  NewWindow(Wid, 3, Title, &GStrWindow, &GStrPanel);
  if (GStrWindow) {
    DisplayMode |= OTHERWINDOW;
    DrawBox(GStrWindow, 1, 1, Wid, 3);
    PushEditOptions();

    SetMenu(GStrMenu, 1);

    GStrOrigBuf = UsrBuf;

    GStrUsrEnd = EndFunc;
    GStrUsrCancel = UsrCancel;
    TabHook = Dummy;
    EnterHook = GStrOK;
    UpHook = Dummy;
    DownHook = Dummy;
    PgUpHook = Dummy;
    PgDownHook = Dummy;
    HomeHook = Dummy;
    EndHook = Dummy;
    TabHook = Dummy;
    EditBuf = GStrBuf;
    LineBSize = BufLen;
    LineWSize = Wid-2; /* LineBSize+1 */;
    LineWOff = 0;
    EditXOff = 2;
    EditYOff = 2;
    ActEditWindow = GStrWindow;
    CharHook = 0;
    GStrUndo();         /* Must be after all the init */
    RefreshEdit();
    top_panel(GStrPanel);
    RedrawKeys();
  }
}

