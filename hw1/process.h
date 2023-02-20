#ifndef _PROCESS_H_
#define _PROCESS_H_

#include <signal.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "parse.h"

typedef struct process
{
  char** argv;
  int argc;
  pid_t pid;
  char completed;
  char stopped;
  char background;
  int status;
  struct termios tmodes;
  int stdIn, stdOut, stdErr;
  struct process* next;
  struct process* prev;
} process;

process* first_process; //pointer to the first process that is launched */

process * create_process(tok_t * inputString);
void add_process(process * p);
void launch_process(process* p);
void run (tok_t * t);
void put_process_in_background (process* p, int cont);
void put_process_in_foreground (process* p, int cont);

#endif
