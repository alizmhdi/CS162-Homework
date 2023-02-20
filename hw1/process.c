#include "process.h"
#include "shell.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "parse.h"


void
setInputStd(process * p, int redirectIndex)
{
  if (p->argv[redirectIndex + 1] == NULL)
    return;
  int file = open(p->argv[redirectIndex + 1], O_RDONLY);
  if (file >= 0)
    p->stdIn = file;
  int i;
  for (i = redirectIndex; i < p->argc; i++) 
    p->argv[i] = NULL;
}

void
setOutputStd(process * p, int redirectIndex)
{
  if (p->argv[redirectIndex + 1] == NULL)
    return;
  int file = open(p->argv[redirectIndex + 1], O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH);
  if (file >= 0)
    p->stdOut = file;
  int i;
  for (i = redirectIndex; i < p->argc; i++) 
    p->argv[i] = NULL;
}

/**
 * Creates a process given the inputString from stdin
 */
process * 
create_process(tok_t * inputString)
{
  process * p = (process*) malloc(sizeof(process));
  p->argv = inputString;
  p->argc = tokenLength(inputString);
  p->pid = NULL;
  p->completed = 0;
  p->stopped = 0;
  p->status = 0;

  p->stdIn = 0;
  p->stdOut = 1;
  p->stdErr = 2;

  int redirectIndex;
  if (p->argv && (redirectIndex = isDirectTok(p->argv, "<")) >= 0)
    setInputStd(p, redirectIndex);
  if (p->argv && (redirectIndex = isDirectTok(p->argv, ">")) >= 0)
    setOutputStd(p, redirectIndex);

  p->argc = tokenLength(p->argv);

  p->prev = NULL;
  p->next = NULL;
  if (p->argv && strcmp(p->argv[p->argc - 1],"&") == 0){
    p->background = 1;
    p->argv[--p->argc] = NULL;
  }

  return p;
}

/**
 * Add a process to our process list
 */
void 
add_process(process * p)
{
  process * prev = first_process;

  while (prev->next)
      prev = prev->next;

  prev->next = p;
  p->prev = prev;
}

/**
 * Executes the process p.
 * If the shell is in interactive mode and the process is a foreground process,
 * then p should take control of the terminal.
 */
void 
launch_process (process *p)
{
  tok_t * inputPath = p->argv;
  tok_t * paths = getToks(getenv("PATH"));
  int pathsLen = tokenLength(paths);
  
  dup2(p->stdIn, 0);
  dup2(p->stdOut, 1);

  if (access(p->argv[0], F_OK) == 0) {
    execv(p->argv[0], &inputPath[0]);
    return;
  }

  char * path = (char *)malloc(MAX_PATH_LENGTH * sizeof(char));

  for(int i = 0; i<pathsLen; i++){
    strcpy(path, paths[i]); strcat(path, "/"); strcat(path, inputPath[0]);
    if (access(path, F_OK) == 0) {
      execv(path, &inputPath[0]);
      free(path);
      return;
    }
    
  }
  free(path);
  fprintf(stdout,"This shell only supports built-ins. Replace this to run programs as commands.\n");
}

void
run (tok_t * t)
{
  process * process = create_process(t);
  add_process(process);

  int cpid = fork();
  if (cpid == 0){
    
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);

    process->pid = getpid();
    launch_process(process);
  }
  else if (cpid > 0)
  {
    process->pid = getpid();
    setpgid(process->pid, process->pid);
    if (!process->background){
      tcsetpgrp(STDIN_FILENO, process->pid);
      int * status;
      waitpid(cpid, status, 2);
    }
  }
}

/* Put a process in the foreground. This function assumes that the shell
 * is in interactive mode. If the cont argument is true, send the process
 * group a SIGCONT signal to wake it up.
 */
void
put_process_in_foreground (process *p, int cont)
{
}

/* Put a process in the background. If the cont argument is true, send
 * the process group a SIGCONT signal to wake it up. */
void
put_process_in_background (process *p, int cont)
{
}
