/* menu.c - Menu subsystem */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include <time.h>

#include "ewterm.h"

struct MenuEntry *(MenuStack[10]);
int MenuSP;

struct MenuEntry *ActMenu;

/* Menu system definition */

struct MenuEntry MainMenu[10] = {
  {"Help", 0, ShowHelp}, 	/*1 */
  {"Cancel", 0, CancelCommand},	/*2 */
  {"Buffer", 0, StartShowBuf}, 	/*3 */
  {"Force", 0, ForceCommand},	/*4 */
  {"SendFile", 0, SendFile},	/*5 */
  {"History", 0, ShowHistoryMenu}, /*6 */
  {"", 0, NULL}, 		/*7 */ /* Reserved when in forms */
  {"Options", 0, ShowOptions}, 	/*8 */
  {"LogOn", 0, StartLogOn}, 	/*9 */
  {"Quit", 0, AskQuit}		/*10 */
};

unsigned int DisplayMode = STATUSLINE + HELPLINE;


void RedrawKeys()
{
  int FreeCnt;
  int i, SpcCnt, OldCnt;
  
  if (DisplayMode & HELPLINE) {

    /* Create menu; count chars that may be spaces */
    FreeCnt = COLS-20;
    for(i=0;i<10;i++)
     FreeCnt -= strlen(ActMenu[i].Text);

    /* Print items */
    wmove(InfoWindow, HelpY, 0);
    
    OldCnt = 0;
    for(i=0;i<10;i++) {
      sprintf(MultiBuf, "%d ", (i+1)%10);
      waddstr(InfoWindow, MultiBuf);
      if (Br[COL_STATUS][0] != '1') wattron(InfoWindow, A_REVERSE);
      waddstr(InfoWindow, ActMenu[i].Text);
      wattroff(InfoWindow, A_REVERSE);
      if (UsingColor) SetBright(InfoWindow, COL_STATUS);
      SpcCnt = FreeCnt*(i+1)/9;
      if (i != 9)
       while(OldCnt < SpcCnt) {
         waddch(InfoWindow, ' ');
         OldCnt++;
       }
    }

    wnoutrefresh(InfoWindow);
  }
}

void RedrawStatus()
{
  int i, Tmp1, Tmp2;

  if (DisplayMode & STATUSLINE) {
    for(i=0;i<COLS;i++) MultiBuf[i] = '-';
    MultiBuf[COLS] = 0;

    /*
-23:59:00-localhost:7380---PATR--OMT1---GTS2---JOB---MASK------ */

    if (DisplayMode & BUFSHOWN) {
      if (LastBuf) {
        Tmp1 = LastBuf->Number - FirstBuf->Number;
        if (Tmp1 == 0) strcpy(MultiBuf+11, "100%");
        else {
          Tmp2 = TopBuf->Number - FirstBuf->Number;
          if (Tmp2 < 0) strcpy(MultiBuf+11, "000%");
          else sprintf(MultiBuf+11, "%03d%%", Tmp2*100/Tmp1);
        }
      }
      else strcpy(MultiBuf+10, "000%");
    }
    else {
      sprintf(MultiBuf+10, "%.10s:%d", HostName, HostPort);
    }

    MultiBuf[strlen(MultiBuf)] = '-';

    {
      time_t ttime = time(NULL);
      struct tm *tm = localtime(&ttime);

      if (tm) {
        sprintf(MultiBuf+1, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
        MultiBuf[strlen(MultiBuf)] = '-';
      }
    }

    if (ActUsrname) {
      strncpy(MultiBuf+27, ActUsrname, strlen(ActUsrname));
    }
    
    if (ActOMT) {
      strncpy(MultiBuf+33, ActOMT, strlen(ActOMT));
    }

    if (ActExchange) {
      strncpy(MultiBuf+40, ActExchange, strlen(ActExchange));
    }

    if (ActJob) {
      sprintf(MultiBuf+47, "%04d", ActJob);
      MultiBuf[strlen(MultiBuf)] = '-';
    }

    if (LastMask > 0) {
      sprintf(MultiBuf+52, "%05d", LastMask);
      MultiBuf[strlen(MultiBuf)] = '-';
    }

    /* -------------------AXFBRIO-OVR-SEMI-CAPS- */

    if (ConvertMode & CV2CAPS) strncpy(MultiBuf+COLS-5, "CAPS", 4);
    if (ConvertMode & ADDSEMI) strncpy(MultiBuf+COLS-10, "SEMI", 4);
    if (ConvertMode & OVERWRITE) strncpy(MultiBuf+COLS-14, "OVR", 3);
    else strncpy(MultiBuf+COLS-14, "INS", 3);
    i=0;
    if (LoggedOff) { MultiBuf[COLS-16] = 'O'; i=1; }
    if (InputRequest) { MultiBuf[COLS-17] = 'I'; i=1; }
    if (PendingCmd) { MultiBuf[COLS-19] = 'B'; i=1; }
    if (!i) MultiBuf[COLS-18] = 'R';
    if (InputRequestHook) MultiBuf[COLS-20] = 'F';
    if (connection && connection->fwmode == FWD_IN) MultiBuf[COLS-21] = 'X';
    if (AutoLogOn == '1') MultiBuf[COLS-22] = 'A';

    mvwaddstr(InfoWindow, 0, 0, MultiBuf);
    wnoutrefresh(InfoWindow);
  }
}

/* Sets new menu. 0 == Go back, Push == 1 means push old menu to the stack */
void SetMenu(struct MenuEntry *WhichMenu, char Push)
{
  if (WhichMenu == 0) ActMenu = MenuStack[--MenuSP];
  else {
    if ((Push) && (WhichMenu != ActMenu)) {
      MenuStack[MenuSP++] = ActMenu;
    }
    ActMenu = WhichMenu;
  }
  RedrawKeys();
}
