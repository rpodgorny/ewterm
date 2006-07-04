/* colors.c - Colors manipulation */

#include <curses.h>
#include <stdio.h>

#include "ewterm.h"

WINDOW *ColorWindow;
PANEL *ColorPanel;

char UsingColor, DenyColors, ForceMono='0';

char Br[11][2] = {"0", "0", "0", "0", "0",
"0", "0", "0", "0", "0", "0"};

int FGs[11] = {
  COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE,
  COLOR_WHITE, COLOR_WHITE, COLOR_WHITE, COLOR_WHITE,
  COLOR_WHITE, COLOR_WHITE, COLOR_WHITE
};

int BGs[11] = {
  COLOR_BLACK, COLOR_BLACK, COLOR_BLACK, COLOR_BLACK,
  COLOR_BLACK, COLOR_BLACK, COLOR_BLACK, COLOR_BLACK,
  COLOR_BLACK, COLOR_BLACK, COLOR_BLACK
};

unsigned char ActCol; /* now drawing with color... */

struct {
void *Next, *Prev;
char Data[30];
} ColorList[] = {
  /* Update color indexes in forms.c, UpdateOptions in options.c and
   * arrays above. */
  {ColorList+1,  0            , "Terminal fg   : "},
  {ColorList+2,  ColorList+0  , "Terminal bg   : "},
  {ColorList+3,  ColorList+1  , "Status/Edit fg: "},
  {ColorList+4,  ColorList+2  , "Status/Edit bg: "},
  {ColorList+5,  ColorList+3  , "Windows fg    : "},
  {ColorList+6,  ColorList+4  , "Windows bg    : "},
  {ColorList+7,  ColorList+5  , "Buffer fg     : "},
  {ColorList+8,  ColorList+6  , "Buffer bg     : "},
  {ColorList+9,  ColorList+7  , "Command fg    : "},
  {ColorList+10, ColorList+8  , "Command bg    : "},
  {ColorList+11, ColorList+9  , "Prompt fg     : "},
  {ColorList+12, ColorList+10 , "Prompt bg     : "},
  {ColorList+13, ColorList+11 , "Error fg      : "},
  {ColorList+14, ColorList+12 , "Error bg      : "},
  {ColorList+15, ColorList+13 , "Help fg       : "},
  {ColorList+16, ColorList+14 , "Help bg       : "},
  {ColorList+17, ColorList+15 , "Link fg       : "},
  {0           , ColorList+16 , "Link bg       : "}
};

void SetBrightness(), SetColorBlack(), SetColorRed(), SetColorGreen();
void SetColorYellow(), SetColorBlue(), SetColorMagenta(), SetColorCyan();
void SetColorWhite(), EndShowColor();

struct MenuEntry ColorMenu[] = {
  {"Bright", 0, SetBrightness},
  {"Black", 0, SetColorBlack},
  {"Red", 0, SetColorRed},
  {"Green", 0, SetColorGreen},
  {"Yellow", 0, SetColorYellow},
  {"Blue", 0, SetColorBlue},
  {"Magenta", 0, SetColorMagenta},
  {"Cyan", 0, SetColorCyan},
  {"White", 0, SetColorWhite},
  {"Back", 0, EndShowColor} 
};



void UpdateHelpColors()
{
  if (UsingColor) SetHelpColors(COL_HELP+1, Br[COL_HELP][0]=='1', COL_LINK+1, Br[COL_LINK][0]=='1');
}

void InitColorMenu()
{
  int i;
  char *TmpPtr;

  /* Init menu variables */
  i = 0;
  do {
    TmpPtr = ColorList[i].Data;
    while((*TmpPtr != ':') && (*TmpPtr)) TmpPtr++;
    if (*TmpPtr) sprintf(TmpPtr+2, "%s%s", Br[i>>1][0] == '1'?"B ":"", ColorMenu[(i&1?BGs[(i>>1)]:FGs[(i>>1)])+1].Text);
  } while(ColorList[i++].Next);
}


void SetBright(WINDOW *Win, int ColNum)
{
  if (!UsingColor) return;

  if (Br[ColNum][0] == '1') wattron(Win, A_BOLD);
  else wattroff(Win, A_BOLD);
}

void CreatePairs()
{
  int i;
  
  for(i=0;i<6;i++) if (FGs[i] != COLOR_WHITE || BGs[i] != COLOR_BLACK) break;
  if (i == 6) return;

  if (UsingColor == 0) start_color();
  UsingColor = 1;
#define IPAIR(n) init_pair(n+1, FGs[n], BGs[n])
			   
  IPAIR(COL_TERM);
  IPAIR(COL_STATUS);
  IPAIR(COL_WIN);
  IPAIR(COL_BUF);
  IPAIR(COL_CMD);
  IPAIR(COL_PROMPT);
  IPAIR(COL_ERR);
  IPAIR(COL_HELP);
  IPAIR(COL_LINK);
#undef IPAIR

/*  init_pair(COL_ERR+2, FGs[COL_ERR], BGs[COL_ERR]); */

  UpdateHelpColors();
}

void ChangeColor(int ToWhat)
{
  int i = ListActNum;

  if (i & 1) BGs[(i>>1)] = ToWhat;
  else FGs[(i>>1)] = ToWhat;

  /* Update data and text */  
  InitColorMenu();
/*
  TmpPtr = ColorList[i].Data;
  while((*TmpPtr != ':') && (*TmpPtr)) TmpPtr++;
  if (*TmpPtr) strcpy(TmpPtr+2, ColorMenu[ToWhat+1].Text);  
*/

  /* Change color pair */  
  if ((has_colors()) && (!DenyColors)) {
    CreatePairs();

    wattron(CUAWindow, ATT_TERM);
    SetBright(CUAWindow, COL_TERM);
    wattron(InfoWindow, ATT_STATUS);
    SetBright(InfoWindow, COL_STATUS);
  }

  /* Refresh screen etc. */
  ListDrawAct();

  UpdateOptions();
  UpdateHelpColors();
}

void SetBrightness()
{
  int i;

  i = ListActNum>>1;

  if (Br[i][0] == '0') Br[i][0] = '1';
  else Br[i][0] = '0';

  UpdateOptions();
  UpdateHelpColors();

  /* Refresh menu */
  InitColorMenu();
  ListActNum ^= 1;
  ListDrawAct();
  ListActNum ^= 1;
  ListDrawAct();
}

void SetColorBlack()
{
  ChangeColor(COLOR_BLACK);
}

void SetColorRed()
{
  ChangeColor(COLOR_RED);
}

void SetColorGreen()
{
  ChangeColor(COLOR_GREEN);
}

void SetColorYellow()
{
  ChangeColor(COLOR_YELLOW);
}

void SetColorBlue()
{
  ChangeColor(COLOR_BLUE);
}

void SetColorMagenta()
{
  ChangeColor(COLOR_MAGENTA);
}

void SetColorCyan()
{
  ChangeColor(COLOR_CYAN);
}

void SetColorWhite()
{
  ChangeColor(COLOR_WHITE);
}


void EndShowColor()
{
  PopEditOptions();  /* Pop pushed by list */
  del_panel(ColorPanel);
  delwin(ColorWindow);
  wnoutrefresh(CUAWindow);
  wnoutrefresh(OptWindow);
  SetMenu(0, 0);

  /* Return options menu */
  PopEditOptions();
  InitList(OptWindow, 1, 1, 22, 14, (void *)OptionList, 0, 0, CMP_NONE);  /* !!!! */
  ListControl(NULL, OptChoosed);
}

void ColorChange()
{
}

void StartShowColor()
{
  InitColorMenu();

  NewWindow(30, 16, "Color settings", &ColorWindow, &ColorPanel);
  if (ColorWindow) {
    SetMenu(ColorMenu, 1);
    RedrawKeys();

    InitList(ColorWindow, 1, 1, 30, 16, (void *)ColorList, 0, 0, CMP_NONE);
    ListControl(NULL, ColorChange);
  }  
}
