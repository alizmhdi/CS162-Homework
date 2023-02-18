#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/wait.h>

#define FALSE 0
#define TRUE 1
#define INPUT_STRING_SIZE 80

#include "io.h"
#include "parse.h"
#include "process.h"
#include "shell.h"


/* Command Lookup table */
typedef int cmd_fun_t (tok_t args[]); /* cmd functions take token array and return int */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

// commands:

int cmd_quit(tok_t arg[]);

int cmd_help(tok_t arg[]);

int cmd_cd(tok_t arg[]);

int cmd_pwd(tok_t arg[]);


fun_desc_t cmd_table[] = {
  {cmd_cd, "cd", "change the current working directory"},
  {cmd_pwd, "pwd", "writes the full pathname of the current working directory"},
  {cmd_help, "?", "show this help menu"},
  {cmd_quit, "quit", "quit the command shell"},
};


int cmd_help(tok_t arg[]) 
{
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    printf("%s - %s\n",cmd_table[i].cmd, cmd_table[i].doc);
  }
  return 1;
}

int cmd_quit(tok_t arg[]) 
{
  printf("Bye\n");
  exit(0);
  return 1;
}

int cmd_cd(tok_t arg[]) 
{
  int status_code = chdir(arg[0]);
  if (status_code != 0){
    printf("cd: no such file or directory: %s\n", arg[0]);
    return 1;
  }
  return 0;
}

int cmd_pwd(tok_t arg[]) 
{
    char * buffer = (char *) malloc(MAX_PATH_LENGTH * sizeof(char));
    char * result = getcwd(buffer, MAX_PATH_LENGTH);
    if (result != NULL) {
      printf("%s\n", buffer);
      return 0;
    }
    return 1;
}


int lookup(char cmd[]) 
{
  int i;
  for (i=0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); i++) {
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0)) return i;
  }
  return -1;
}


/**
 * Add a process to our process list
 */
void add_process(process * p)
{
  process * prev = first_process;

  while (prev->next)
      prev = prev->next;

  prev->next = p;
  p->prev = prev;
}

/**
 * Creates a process given the inputString from stdin
 */
process * create_process(tok_t * inputString)
{
  process * p = (process*) malloc(sizeof(process));
  p->argv = inputString;
  p->argc = tokenLength(inputString);
  p->pid = getpid();
  p->completed = 0;
  p->stopped = 0;
  p->status = 0;

  p->stdIn = 0;
  p->stdOut = 1;
  p->stdErr = 2;

  p->prev = NULL;
  p->next = NULL;
  if (p->argv && strcmp(p->argv[p->argc - 1],"&") == 0){
    p->background = 1;
    p->argv[--p->argc] = NULL;
  }

  return p;
}


void init_shell()
{
  /* Check if we are running interactively */
  shell_terminal = STDIN_FILENO;

  /** Note that we cannot take control of the terminal if the shell
      is not interactive */
  shell_is_interactive = isatty(shell_terminal);

  if(shell_is_interactive){

    /* force into foreground */
    while(tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp()))
      kill( - shell_pgid, SIGTTIN);

    shell_pgid = getpid();
    /* Put shell in its own process group */
    if(setpgid(shell_pgid, shell_pgid) < 0){
      perror("Couldn't put the shell in its own process group");
      exit(1);
    }

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);
    tcgetattr(shell_terminal, &shell_tmodes);
  }
  first_process = create_process(NULL);
}


int shell (int argc, char *argv[]) 
{
  char *s = malloc(INPUT_STRING_SIZE+1);			/* user input string */
  tok_t *t;			/* tokens parsed from input */
  int lineNum = 0;
  int fundex = -1;
  pid_t pid = getpid();		/* get current processes PID */
  pid_t ppid = getppid();	/* get parents PID */
  pid_t cpid, tcpid, cpgid;
  
  init_shell();

  // printf("%s running as PID %d under %d\n",argv[0],pid,ppid);

  lineNum=0;
  // fprintf(stdout, "%d: ", lineNum);
  while ((s = freadln(stdin))){
    t = getToks(s); /* break the line into tokens */
    fundex = lookup(t[0]); /* Is first token a shell literal */
    if(fundex >= 0) cmd_table[fundex].fun(&t[1]);
    else if (t[0] != NULL) {
      process * process = create_process(t);
      add_process(process);

      cpid = fork();
      if (cpid == 0){
        process->pid = getpid();
        launch_process(process);
      }
      else if (cpid > 0)
      {
        setpgid(pid, pid);
        process->pid = pid;
        if (!process->background){
          int * status;
          waitpid(-1, status, WUNTRACED);
        }
      }
    }
    // fprintf(stdout, "%d: ", lineNum);
  }
  return 0;
}
