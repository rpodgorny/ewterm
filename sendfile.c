/* sendfile.c - sending of files to exchange */

#include <curses.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "ewterm.h"


char SendFileDestFName[18] = "";
char SendFileFName[256] = "";
char SendFilePath[260] = "";

static FILE *Fl;

extern void AppendSlash(char *);
extern char FRqName[];

int Cancel = 0;


void SendFileCleanup()
{
  InputRequestHook = NULL;
  NoInputRequestHook = NULL;
  CancelHook = NULL;
  fclose(Fl); Fl = NULL;
}

int SendFileTestCancel()
{
  if (Cancel) {
    SendFileCleanup();
    CancelCommand();
    return 1;
  }
  return 0;
}


void SendFileError()
{
  AddEStr("EXEC EDTS8; failed.\n", 0, 0);
  SendFileCleanup();
}

void SendFileCancel()
{
  Cancel = 1;
}


void SendFileFinish()
{
  AddCommandToQueue("END", 2);
  SendFileCleanup();
}

void SendFileConfirm()
{
  if (SendFileTestCancel()) return;
  AddCommandToQueue("Y", 2);
  InputRequestHook = SendFileFinish;
}

void SendFileWrite()
{
  char s[256];
  if (SendFileTestCancel()) return;
  snprintf(s, 256, "WRITE %s", SendFileDestFName);
  AddCommandToQueue(s, 2);
  InputRequestHook = SendFileConfirm;
}

void SendFileSend()
{
  char buf[4096];

  if (SendFileTestCancel()) return;
  if (!fgets(buf, 4096, Fl)) {
    AddCommandToQueue(".", 2);
    InputRequestHook = SendFileWrite;
  } else {
    AddCommandToQueue(buf, ConvertMode);
  }
}

void SendFileSetup()
{
  if (SendFileTestCancel()) return;
  AddCommandToQueue("1", 2);
  InputRequestHook = SendFileSend;
  NoInputRequestHook = SendFileError;
}

void SendFileStart()
{
  if (LoggedOff || InputRequest) {
    AddEStr("You can't start sending a file when logged off or while pending input request.\n", 0, 0);
    return;
  }

  strcpy(SendFilePath, FRqDir);
  AppendSlash(SendFilePath);
  UpdateOptions();

  Fl = fopen(SendFileFName, "r");
  if (Fl == 0) beep();

  if (Fl) {
    AddCommandToQueue("EXEC EDTS8;", 2);
    InputRequestHook = SendFileSetup;
    //NoInputRequestHook = SendFileError; /* > will be here for EXEC EDTS8; */
    CancelHook = SendFileCancel;
  } else {
    AddEStr("Cannot open the file for sending.\n", 0, 0);
  }
}

void SendFileDest()
{
  strncpy(SendFileDestFName, FRqName, 16); SendFileDestFName[17] = 0;
  GetString(SendFileDestFName, 18, "Destination file name", SendFileStart, NULL);
}

void SendFile()
{
  strcpy(SendFileFName, SendFilePath);
  FileRequest(SendFileFName, "Source file name", SendFileDest);
}
