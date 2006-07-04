/* help.c - Help subsystem; now it's forms wrapper */

/* This is very messy as the identifiers naming convention is totally
 * mixed up. Someone should clean this up. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <curses.h>
#include <ctype.h>
#include "ewterm.h"

extern int goto_show_help(void *, void *);
extern void goto_parent(void *);
extern int goto_node(char *);

extern void start_loop();
extern void end_loop(int);
extern void init_forms();
extern void done_forms();
extern void back_from_help();

extern void start_loop_help();
extern void end_loop_help(int);
extern void init_show_help();
extern void done_show_help();

extern void forms_border_arrow(char);

extern void *node;
extern void *afield;

int in_help = 0;
int forms_active = 0;

WINDOW *HelpWindow;
PANEL *HelpPanel;
void (*HOldEditHook)();
int HOldDMode;
char HelpDontRefresh;

void HelpGotoHelp(), HelpGotoIndex(), HelpGotoParent(), HelpGotoCmdHelp(), HelpSend();
void HideHelp(), HideForms(), FromForms(), ToForms();

struct MenuEntry FormsMenu[10] = {
  {"Opt Help", 0, HelpGotoHelp},
  {"Root", 0, HelpGotoIndex},
  {"Parent", 0, HelpGotoParent},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"Buffer", 0, FromForms},
  {"Cmd Help", 0, HelpGotoCmdHelp},
  {"Send", 0, HelpSend},
  {"Back", 0, HideForms} 
};

struct MenuEntry HelpMenu[10] = {
  {"", 0, NULL},
  {"", 0, NULL},
  {"Back", 0, HideHelp},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"Back", 0, HideHelp}
};

void HelpEditHook(int Key)
{
  char Type;

  Type = 0;
  if (Key < 256) Type = 1;
  else switch(Key) {
    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_UP:
    case KEY_DOWN:
    case KEY_HOME:
    case KEY_END:
    case KEY_PPAGE:
    case KEY_NPAGE:
    case KEY_IC:
    case '\t':
    case KEY_BACKSPACE:
    case KEY_ENTER:
    case KEY_F(9): /* XXX */
      Type = 1;
      break;
  }
  if ((Type == 1) && (!IsNonModal)) {
    if (in_help)
      end_loop_help(Key);
    else
      end_loop(Key);

    if (HelpWindow) {
      /* in_help value may be different here ? */
      if (in_help)
	start_loop_help();
      else
	start_loop();
    }
  } else {
    ProcessSpecialKey(Key);
  }
}

void
MkHelpWin()
{
  NewWindow(COLS-2, CUALines-2, NULL, &HelpWindow, &HelpPanel);
  top_panel(HelpPanel);
}

void
DelHelpWin()
{
  del_panel(HelpPanel);
  delwin(HelpWindow);
}

void ShowHelp()
{
  if (HelpPanel) {
    beep();
    return;
  }

  MkHelpWin();
  HOldDMode = DisplayMode;
  DisplayMode |= HELPSHOWN+OTHERWINDOW;

  SetMenu(FormsMenu, 1);

  HOldEditHook = EditHook;
  EditHook = HelpEditHook;
  RedrawKeys();

  touchwin(InfoWindow);

  forms_active = 1;
  init_forms();
  if (LineBuf) {
    char *UpperBuf = strdup(LineBuf);
    char *UpperPtr = UpperBuf;

    while (*UpperPtr) { *UpperPtr = toupper(*UpperPtr); UpperPtr++; }

    if (goto_node(UpperBuf) < 0 && goto_node("OMEXCH") < 0) {
      AddEStr("Forms file not found!\n", 0, 0);
      HideForms();
      return;
    }

    free(UpperBuf);
  } else {
    if (goto_node("OMEXCH") < 0) {
      AddEStr("Forms file not found!\n", 0, 0);
      HideForms();
      return;
    }
  }
  start_loop();
}

void HideForms()
{
  done_forms();
  forms_active = 0;
  DelHelpWin();

  EditHook = HOldEditHook;
  DisplayMode = HOldDMode&(~HELPSHOWN);
  HelpPanel = 0;
  HelpWindow = 0;
  update_panels();

  ActMenu[6].Text = "Buffer";
  ActMenu[6].Addr = FromForms;
  SetMenu(0, 0);
  RedrawKeys();
}

void FromForms()
{
  EditHook = HOldEditHook;
  DisplayMode = HOldDMode|HELPSHOWN;

  ActMenu[6].Text = "Forms";
  ActMenu[6].Addr = ToForms;
  RedrawKeys();

  forms_border_arrow('v');
  forms_active = 0;
}

void ToForms()
{
  forms_active = 1;
  forms_border_arrow('^');

  HOldDMode = DisplayMode;
  DisplayMode |= HELPSHOWN+OTHERWINDOW;

  HOldEditHook = EditHook;
  EditHook = HelpEditHook;

  ActMenu[6].Text = "Buffer";
  ActMenu[6].Addr = FromForms;
  RedrawKeys();

  touchwin(InfoWindow);
}

void HelpGotoCmdHelp()
{
  if (goto_show_help(node, NULL)<0) return;
  in_help = 1;
  SetMenu(HelpMenu, 1);
  RedrawKeys();
}

void HelpGotoHelp()
{
  if (goto_show_help(node, afield)<0) return;
  in_help = 1;
  SetMenu(HelpMenu, 1);
  RedrawKeys();
}

void HideHelp()
{
  done_show_help();
  in_help = 0;

  SetMenu(0, 0);

  back_from_help();
  start_loop();
}

void HelpGotoIndex()
{
  goto_node("OMEXCH");
  start_loop();
}

void HelpGotoParent()
{
  goto_parent(node);
  start_loop();
}

void HelpBackHist()
{
#if 0
  GotoBackHist();
#endif
}

void HelpSend()
{
  HelpEditHook(KEY_F(9));
}
