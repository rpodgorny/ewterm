/* frq.c - Filerequester */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <curses.h>
#include <unistd.h>
#include <dirent.h>
#include "ewterm.h"

WINDOW *FRqWindow;
PANEL *FRqPanel;
void (*FRqUsrEnd)();
char *FRqOrigBuf;
char FRqDir[256], FRqName[256], FRqOrigCWD[256];
struct TListEntry *FRqListFirst, *FRqListLast;

#define FRQWIDTH 40

#define FRQ_LIST 0
#define FRQ_DIR 1
#define FRQ_NAME 2

void FRqOK(), FRqSetList(), FRqSetDir(), FRqSetName(), FRqParent(), FRqCancel();

struct MenuEntry FRqMenu[10] = {
  {"OK", 0, FRqOK},
  {"List", 0, FRqSetList},
  {"Dir", 0, FRqSetDir},
  {"Name", 0, FRqSetName},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"", 0, NULL},
  {"Parent", 0, FRqParent},
  {"Cancel", 0, FRqCancel}
};

char FRqActive;

void FRqFreeList()
{
  struct TListEntry *TmpPtr, *TmpPtr2;

  TmpPtr = FRqListFirst;
  while(TmpPtr) {
    TmpPtr2 = TmpPtr->Next;
    free(TmpPtr);
    TmpPtr = TmpPtr2;
  }
  FRqListFirst = 0;
  FRqListLast = 0;
}

void FRqDone(char WasOK)
{
  char *TmpPtr;

  if (FRqActive == FRQ_LIST) PopEditOptions();  /* Pop pushed by list */
  FRqFreeList();
  DisplayMode &= 255-NOEDIT;
  del_panel(FRqPanel);
  delwin(FRqWindow);
  PopEditOptions();
  DisplayMode &= 65535-HIDE;
  FRqPanel = 0;
  FRqWindow = 0;
  update_panels();
  SetMenu(0, 0);

  chdir(FRqOrigCWD);

  if (WasOK) {
    /* Concaneate strings */
    strcpy(FRqOrigBuf, FRqDir);
    TmpPtr = FRqOrigBuf;
    while(*TmpPtr) TmpPtr++;
    if (*(TmpPtr-1) == '/') TmpPtr--;
    *TmpPtr++ = '/';
    strcpy(TmpPtr, FRqName);

    /* Call original function */
    FRqUsrEnd();
  }
}

void FRqOK()
{
  FRqDone(1);
}

void FRqCancel()
{
  FRqDone(0);
}

int FRqTestName(const struct dirent *TmpEnt)
{
  return(TmpEnt->d_name[0] != '.');
}

void FRqRefreshDir()
{
  struct dirent **TmpDE;
  struct TListEntry *TmpEntry;
  int Entries, Len, i;
  char *TmpPtr, *DstPtr, *TmpPtr2, CopyBack;
  DIR *TmpDir;

  FRqFreeList();

  /* Expand '~' */

  TmpPtr = FRqDir;
  Len = 0;
  DstPtr = MultiBuf;
  CopyBack = 0;
  while((*TmpPtr) && (Len < 250)) {
    if (*TmpPtr == '~') {
      CopyBack = 1;
      TmpPtr++;
      if (Len > 220) break;
      if ((*TmpPtr == 0) || (*TmpPtr == '/')) {
        TmpPtr2 = getenv("HOME");
        if (TmpPtr2 == 0) sprintf(DstPtr, "/home/%s", getenv("LOGNAME"));
        else strcpy(DstPtr, TmpPtr2);
      }
      else strcpy(DstPtr, "/home/");
      while(*DstPtr) {
        DstPtr++;
        Len++;
      }
    }
    else {
      *DstPtr++ = *TmpPtr++;
      Len++;
    }
  }
  *DstPtr = 0;
  if (CopyBack) strcpy(FRqDir, MultiBuf);

  /* Try to read dir */

  if (( (FRqDir[0] == 0)?0:chdir(FRqDir) ) == 0) {
    getcwd(FRqDir, 256);
    /* Refresh directory gadget */
    if (FRqActive == FRQ_DIR) {
      LineBLen = strlen(FRqDir);
      LineBPos = LineBLen;
    }
    PushEditOptions();
    EditBuf = FRqDir;
    EditXOff = 2;
    EditYOff = 16;
    RefreshEdit();
    PopEditOptions();

    Entries = scandir(".", &TmpDE, FRqTestName, alphasort);
    /* Process all entries and add them to our list */
    for(i=0;i<Entries;i++) {
      TmpPtr = TmpDE[i]->d_name;
      Len = strlen(TmpPtr);
      TmpEntry = (void *)malloc(sizeof(*TmpEntry)+FRQWIDTH+3);
      strncpy(TmpEntry->Data, TmpPtr, Len);
      TmpDir = opendir(TmpPtr);
      if (TmpDir) {
        closedir(TmpDir);
        TmpEntry->Data[Len] = '/';
        Len++;
      }
      if (Len < FRQWIDTH) strncpy(TmpEntry->Data+Len, "                                        ", FRQWIDTH-Len);
      TmpEntry->Data[FRQWIDTH] = 0;

      TmpEntry->Prev = FRqListLast;
      FRqListLast = TmpEntry;
      TmpEntry->Next = 0;
      if (FRqListFirst) TmpEntry->Prev->Next = TmpEntry;
      else FRqListFirst = TmpEntry;
    }
  }
  else Entries = 0;

  InitList(FRqWindow, 2, 2, FRQWIDTH, 12, FRqListFirst, 0, 0, CMP_CASE);
  if (FRqActive == FRQ_LIST) ListDrawAct();
}

void FRqListProcessEnter()
{
  char *TmpPtr, *TmpPtr2;

  /* Result is in ListAct->Data */
  TmpPtr = strchr(ListAct->Data, '/');
  if (TmpPtr) {
    /* Dir selected */
    *TmpPtr = 0;
    strcpy(FRqDir, ListAct->Data);
    FRqRefreshDir();
  }
  else {
    /* Name selected */
    strcpy(FRqName, ListAct->Data);
    /* Strip spaces */
    TmpPtr = FRqName;
    TmpPtr2 = FRqName;
    while(*TmpPtr) {
      if (*TmpPtr != ' ') TmpPtr2 = TmpPtr;
      TmpPtr++;
    }
    *(TmpPtr2+1) = 0;

    /* End */
    FRqDone(1);
  }
}

void FRqSetList()
{
  if (FRqListFirst == 0) FRqSetDir();
  else {
    FRqActive = FRQ_LIST;
    ListControl(FRqSetDir, FRqListProcessEnter);
  }
}

void FRqDirChanged()
{
  FRqRefreshDir();
  FRqSetName();
}

void FRqSetDir()
{
  if (FRqActive == FRQ_LIST) PopEditOptions();
  FRqActive = FRQ_DIR;
  DisplayMode &= 255-NOEDIT;
  TabHook = FRqSetName;
  EnterHook = FRqDirChanged;
  EditBuf = FRqDir;
  LineBLen = strlen(FRqDir);
  EditYOff = 16;
  EditEnd();
  EditYOff = 16;
  EditEnd();
  RefreshEdit();
}

void FRqSetName()
{
  if (FRqActive == FRQ_LIST) PopEditOptions();
  FRqActive = FRQ_NAME;
  DisplayMode &= 255-NOEDIT;
  TabHook = FRqSetList;
  EnterHook = FRqOK;
  EditBuf = FRqName;
  EditYOff = 19;
  LineBLen = strlen(FRqName);
  EditEnd();
  RefreshEdit();
}

void FRqParent()
{
  strcpy(FRqDir, "..");
  FRqRefreshDir();
}

void CenterText(WINDOW *Win, int x, int y, int w, char *Text)
{
  mvwaddstr(Win, y, x+(w-strlen(Text))/2, Text);
}

void FileRequest(char *UsrBuf, char *Title, void (*EndFunc)())
{
  char *TmpPtr;
  int Len, Len2;

  NewWindow(FRQWIDTH+2, 20, Title, &FRqWindow, &FRqPanel);
  if (FRqWindow) {
    DrawBox(FRqWindow, 1, 1, FRQWIDTH+2, 14);
    CenterText(FRqWindow, 1, 1, FRQWIDTH+2, " FILE LIST ");

    DrawBox(FRqWindow, 1, 15, FRQWIDTH+2, 3);
    CenterText(FRqWindow, 1, 15, FRQWIDTH+2, " DIRECTORY ");

    DrawBox(FRqWindow, 1, 18, FRQWIDTH+2, 3);
    CenterText(FRqWindow, 1, 18, FRQWIDTH+2, " FILE NAME ");

    FRqName[0] = 0;
    PushEditOptions();

    SetMenu(FRqMenu, 1);

    FRqOrigBuf = UsrBuf;

    FRqUsrEnd = EndFunc;
    TabHook = Dummy;
    EnterHook = FRqOK;
    TabHook = FRqSetList;
    UpHook = Dummy;
    DownHook = Dummy;
    PgUpHook = Dummy;
    PgDownHook = Dummy;
    EditBuf = FRqName;
    LineBSize = 255;
    LineWSize = 40;
    LineWOff = 0;
    EditXOff = 2;
    EditYOff = 19;
    ActEditWindow = FRqWindow;

    RefreshEdit();
    top_panel(FRqPanel);
    RedrawKeys();

    getcwd(FRqOrigCWD, 256);

    Len = 0;
    TmpPtr = UsrBuf;
    Len2 = -1;
    while(*TmpPtr) {
      if (*TmpPtr == '/') Len2 = Len;
      TmpPtr++;
      Len++;
    }
    if (Len2 >= 0) {
      strncpy(FRqDir, UsrBuf, Len2);
      FRqDir[Len2] = 0;
    }

    FRqActive = FRQ_DIR;
    FRqDirChanged();
    FRqSetList();
  }
}
