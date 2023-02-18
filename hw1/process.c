#include "process.h"
#include "shell.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <termios.h>

#include "parse.h"


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
  char * path = (char *)malloc(MAX_PATH_LENGTH * sizeof(char));

  for(int i = 0; i<pathsLen; i++){
    strcpy(path, paths[i]); strcat(path, "/"); strcat(path, inputPath[0]);
    if (access(path, F_OK) == 0) {
      execv(path, &inputPath[0]);
      return;
    }
  }
  if (access(p->argv[0], F_OK) == 0)
    execv(p->argv[0], &inputPath[0]);
  else
    fprintf(stdout, "This shell only supports built-ins. Replace this to run programs as commands.\n");
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
