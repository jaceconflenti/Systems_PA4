// 
// tsh - A tiny shell program with job control
// 
// <Put your name and login ID here>
// Steven Conflenti, Cameron Taylor
// stco8901, cata5942

using namespace std;

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>

#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"

//
// Needed global variable definitions
//

static char prompt[] = "tsh> ";
int verbose = 0;

//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
// 

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

//
// main - The shell's main routine 
//
int main(int argc, char **argv) 
{
  int emit_prompt = 1; // emit prompt (default)

  //
  // Redirect stderr to stdout (so that driver will get all output
  // on the pipe connected to stdout)
  //
  dup2(1, 2);

  /* Parse the command line */
  char c;
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h':             // print help message
      usage();
      break;
    case 'v':             // emit additional diagnostic info
      verbose = 1;
      break;
    case 'p':             // don't print a prompt
      emit_prompt = 0;  // handy for automatic testing
      break;
    default:
      usage();
    }
  }

  //
  // Install the signal handlers
  //

  //
  // These are the ones you will need to implement
  //
  Signal(SIGINT,  sigint_handler);   // ctrl-c
  Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
  Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child

  //
  // This one provides a clean way to kill the shell
  //
  Signal(SIGQUIT, sigquit_handler); 

  //
  // Initialize the job list
  //
  initjobs(jobs);

  //
  // Execute the shell's read/eval loop
  //
  for(;;) {
    //
    // Read command line
    //
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }

    char cmdline[MAXLINE];

    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
      app_error("fgets error");
    }
    //
    // End of file? (did user type ctrl-d?)
    //
    if (feof(stdin)) {
      fflush(stdout);
      exit(0);
    }

    //
    // Evaluate command line
    //
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  } 

  exit(0); //control never reaches here
}
  
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
// 
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline) 
{
  /* Parse command line */
  //
  // The 'argv' vector is filled in by the parseline
  // routine below. It provides the arguments needed
  // for the execve() routine, which you'll need to
  // use below to launch a process.
  //
  char *argv[MAXARGS];

  //
  // The 'bg' variable is TRUE if the job should run
  // in background mode or FALSE if it should run in FG
  //
  int bg = parseline(cmdline, argv); 
  if (argv[0] == NULL)  
    return;   /* ignore empty lines */

  //process id and mask
  pid_t pid;
  sigset_t mask;
  
  //checks if line is builtin
  if(!builtin_cmd(argv)){
	//blocks SIGCHILD signals
    if(sigemptyset(&mask) != 0){
      printf("sigemptyset error");
    }
    if(sigaddset(&mask, SIGCHLD) != 0){
      printf("sigaddset error");
    }
    if(sigprocmask(SIG_BLOCK, &mask, NULL) != 0){
      printf("sigprocmask error");
    }
	  
	//forking
    pid = fork();
    if(pid < 0){
      printf("forking error");
    }
	
	//check if child - unblock mask, new process group, run
    else if(pid == 0){
      if(sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0){
		printf("sigprocmask error");
      }
      if(setpgid(0, 0) < 0){
		printf("setpgid error");
      }
      if(execv(argv[0], argv) < 0){
		printf("%s: Command not found\n", argv[0]);
		exit(1);
      }
	}
	
	//parent, add job, unblock, run
	else{
		if(bg == 0){
		  addjob(jobs, pid, FG, cmdline);
		}
		else{
		  addjob(jobs, pid, BG, cmdline);
		}
		if(sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0){
		  printf("sigprocmask error");
		}
		if(bg == 0){
		  waitfg(pid);
		}
		else{
		  printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
		}
	}
  }
	
  return;
}


/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need 
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv) 
{
  //string cmd(argv[0]);
  
  //quits shell
  if(!strcmp("quit", argv[0])){
    exit(0);
  }
  //lists jobs
  else if(!strcmp("jobs", argv[0])){
    listjobs(jobs);
    return 1;
  }
  //puts process in fg or bg
  else if(!strcmp("fg", argv[0]) || !strcmp("bg", argv[0])){
    do_bgfg(argv);
    return 1;
  }
  return 0;     /* not a builtin command */
}

/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv) 
{
  struct job_t *jobp=NULL;
    
  /* Ignore command if no argument */
  if (argv[1] == NULL) {
    printf("%s command requires PID or %%jobid argument\n", argv[0]);
    return;
  }
    
  /* Parse the required PID or %JID arg */
  if (isdigit(argv[1][0])) {
    pid_t pid = atoi(argv[1]);
    if (!(jobp = getjobpid(jobs, pid))) {
      printf("(%d): No such process\n", pid);
      return;
    }
  }
  else if (argv[1][0] == '%') {
    int jid = atoi(&argv[1][1]);
    if (!(jobp = getjobjid(jobs, jid))) {
      printf("%s: No such job\n", argv[1]);
      return;
    }
  }	    
  else {
    printf("%s: argument must be a PID or %%jobid\n", argv[0]);
    return;
  }

  //
  // You need to complete rest. At this point,
  // the variable 'jobp' is the job pointer
  // for the job ID specified as an argument.
  //
  // Your actions will depend on the specified command
  // so we've converted argv[0] to a string (cmd) for
  // your benefit.
  //
  //string cmd(argv[0]);
  
  //sets pid and jid, uses jid to find job
  pid_t pid = atoi(argv[1]);
  int jid = atoi(&argv[1][1]);
  //jobp = getjobpid(jobs, pid);
  jobp = getjobjid(jobs, jid);

  //makes sure job exists
  if(jobp != NULL){
	//sets pid and jid to that of their job
    pid = jobp -> pid;
    jid = jobp -> jid;
    
    //if job is stopped, change it based on fg or bg command
    if(jobp -> state == ST){
      if(!strcmp("bg", argv[0])){
		printf("[%d] (%d) %s", jid, pid, jobp -> cmdline);
		jobp -> state = BG;
		kill(-pid, SIGCONT);
      }
      else if(!strcmp("fg", argv[0])){
		jobp -> state = FG;
		kill(-pid, SIGCONT);
		waitfg(pid);
      }
    } 
	 
	//if job is in bg and command is fg, move to fg
    if(jobp -> state == BG){
      if(!strcmp("fg", argv[0])){
		jobp -> state = FG;
		waitfg(pid);
      }
    }
	 
	//if job is in fg and command is bg, move to bg
    if(jobp -> state == FG){
      if(!strcmp("bg", argv[0])){
		printf("[%d] (%d) %s", jid, pid, jobp -> cmdline);
		jobp -> state = BG;
		kill(-pid, SIGCONT);
      }
    }
  }  

  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid)
{
  //while the pid is the fg pid, wait until not in fg
  while(pid == fgpid(jobs)) {
    sleep(0);
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//


/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.  
//
void sigchld_handler(int sig) 
{
  //pid and job state
  int state;  
  pid_t pid;  
    
  //wait for child processes to be handled based on state
  while((pid = waitpid(fgpid(jobs), &state, WNOHANG|WUNTRACED)) > 0){  
	//suspend process
    if (WIFSTOPPED(state)){  
      sigtstp_handler(20);  
    } 
    //interrupt process
    else if(WIFSIGNALED(state)){  
      sigint_handler(-2);  
    }
    //delete process
    else if(WIFEXITED(state)){  
      deletejob(jobs, pid);  
    }  
  }  
  
  
  if(errno != ECHILD){  
    printf("waitpid error\n");   
  } 
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.  
//
void sigint_handler(int sig) 
{
  //set up pid and jid
  pid_t pid = fgpid(jobs);
  int jid = pid2jid(pid);
	
  //kill and print termination, delete process
  if(pid != 0){ 
    kill(-pid, SIGINT);
    if(sig < 0){  
      printf("Job [%d] (%d) terminated by signal %d\n", jid, pid, -sig);  
      deletejob(jobs, pid);  
    }    
  }  
  return;
}

/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.  
//
void sigtstp_handler(int sig) 
{
  //set up pid and jid
  pid_t pid = fgpid(jobs);
  int jid = pid2jid(pid);
	
  //print stop message, set state, kill
  if (pid != 0){ 
    printf("Job [%d] (%d) Stopped by signal %d\n", jid, pid, sig);   
    getjobpid(jobs, pid) -> state = ST;  
    kill(-pid, SIGTSTP);  
  }  
  return;
}

/*********************
 * End signal handlers
 *********************/
