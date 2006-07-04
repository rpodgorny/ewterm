/* bfu.c - basic user interface, misc support functions */

#include <curses.h>
#include <string.h>

#include "ewterm.h"

WINDOW *MainW;
WINDOW *CUAWindow, *InfoWindow;
PANEL *CUAPanel, *InfoPanel;

int CUALines;		/* Height of CUAWindow */
int InfoLines;		/* Height of InfoWindow */
int EditY;		/* Y pos of edit line */
int HelpY;		/* Y pos of help line */
char UpdateCUAW = 1;
char IsNonModal, ShadowHelp = 0;
int ActKey;		/* Pressed key. For use by CharHook */

unsigned char MultiBuf[256];	/* promiscuous buf */

void (*UserYes)();
void (*UserNo)();

void (*EditHook)();

void (*EnterHook)();
void (*UpHook)();
void (*DownHook)();
void (*PgUpHook)();
void (*PgDownHook)();
void (*HomeHook)();
void (*EndHook)();
void (*TabHook)();
void (*CharHook)();	/* Used for ASCII chars and CTRL-H */

void BoolYes();
void BoolNo();

struct MenuEntry BoolMenu[10] = {
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {0, 0, BoolYes},
  {"Cancel", 0, BoolNo}
};



/* Recreates (and updates) windows/panels according to current display mode */
void RecreateWindows()
{
  int i;

  if (CUAPanel) del_panel(CUAPanel);
  if (CUAWindow) delwin(CUAWindow);
  if (InfoPanel) del_panel(InfoPanel);
  if (InfoWindow) delwin(InfoWindow);

  InfoLines = 0;
  EditY = 0;
  HelpY = 0;
  if (DisplayMode & STATUSLINE) {
    InfoLines++;
    EditY++;
    HelpY++;
  }
  /* LINEMODE */
  InfoLines++;
  HelpY++;

  if (DisplayMode & HELPLINE) InfoLines++;
  CUALines = LINES-InfoLines;

  InfoWindow = newwin(InfoLines, 0, CUALines, 0);
  InfoPanel = (void *)new_panel(InfoWindow);
  CUAWindow = newwin(CUALines, 0, 0, 0);
  CUAPanel = (void *)new_panel(CUAWindow);
  scrollok(CUAWindow, TRUE);
  keypad(CUAWindow, TRUE);
  //scrollok(InfoWindow, TRUE);
  keypad(InfoWindow, TRUE);
  UpdateCUAW = 1;

  if (UsingColor) {
    wattron(InfoWindow, ATT_STATUS);
    SetBright(InfoWindow, COL_STATUS);
    wattron(CUAWindow, ATT_TERM);
    SetBright(CUAWindow, COL_TERM);

    /* Clear window */
    if (BGs[0] != 0) {
      for(i=0;i<CUALines;i++) {
	wmove(CUAWindow, i, 0);
        FillLine(CUAWindow, COL_TERM);
      }
    }
  }

  if (NumBuf < CUALines) CUATop = FirstBuf;
  else {
    CUATop = LastBuf;
    for(i=2;i<CUALines;i++) CUATop = CUATop->Prev;
  }
  RedrawCUAWindow();

  ActEditWindow = InfoWindow;
  EditYOff = EditY;

  RedrawStatus();
  RedrawKeys();
}

void DrawBox(WINDOW *Win, int x, int y, int w, int h)
{
  int i;

  for(i=x;i<(w+x);i++) {
    mvwaddch(Win, y, i, ACS_HLINE);
    mvwaddch(Win, y+h-1, i, ACS_HLINE);
  }
  for(i=y;i<(h+y);i++) {
    mvwaddch(Win, i, x, ACS_VLINE);
    mvwaddch(Win, i, x+w-1, ACS_VLINE);
  }
  mvwaddch(Win, y, x, ACS_ULCORNER);
  mvwaddch(Win, y, x+w-1, ACS_URCORNER);
  mvwaddch(Win, y+h-1, x, ACS_LLCORNER);
  mvwaddch(Win, y+h-1, x+w-1, ACS_LRCORNER);
}

void NewWindow(int w, int h, char *Title, WINDOW **WindowPtr, PANEL **PanelPtr)
{
  #define WPtr *WindowPtr

  WPtr = newwin(h+2, w+2, (CUALines-h)/2-1, (COLS-w)/2-1);
  if (WPtr == NULL) return;
  //scrollok(WPtr, TRUE);
  keypad(WPtr, TRUE);
  *PanelPtr = (void *)new_panel(WPtr);

  if ((WPtr != 0) && (Title != 0)) {
    if (UsingColor) {
      wattron(WPtr, ATT_WIN);
      SetBright(WPtr, COL_WIN);
    }
    DrawBox(WPtr, 0, 0, w+2, h+2);
    wattron(WPtr, A_REVERSE);
    mvwaddstr(WPtr, 0, (w-strlen(Title))/2+1, Title);
    wattroff(WPtr, A_REVERSE);
  }

  #undef WPtr
}

/* Asks bool value in non-modal mode */
void AskBool(char *YesText, void (*YesRout)(), void (*NoRout)())
{
  UserYes = YesRout;
  UserNo = NoRout;
  IsNonModal = 1;
  BoolMenu[8].Text = YesText;
  SetMenu(BoolMenu, 1);
}

void BoolYes()
{
  IsNonModal = 0;
  SetMenu(0, 0);
  if (UserYes)
    UserYes();
  UserYes = NULL;
}

void BoolNo()
{
  IsNonModal = 0;
  SetMenu(0, 0);
  if (UserNo)
    UserNo();
  UserNo = NULL;
}

void DoQuit()
{
  DoneQuit();
}

void AskQuit()
{
  AskBool("Really quit", DoQuit, NULL);
}

/* Dummy function */
void Dummy()
{
}

void InitScr()
{
  initscr();
  MainW = stdscr;
  savetty();
  nonl();
  noecho();
  raw();
  clear();
  keypad(stdscr, TRUE);
  scrollok(stdscr, FALSE);
  intrflush(stdscr, FALSE);
  meta(stdscr, TRUE);
  refresh();

  if (ForceMono == '1') DenyColors = 1;

  if ((has_colors()) && (!DenyColors))
    CreatePairs();
}
