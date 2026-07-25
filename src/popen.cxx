/* Copyright (c) 2020-21 Project re-Isearch and its contributors: See CONTRIBUTORS.
It is made available and licensed under the Apache 2.0 license: see LICENSE */


//
// popen.cxx
// Handle pipes and processes
//
// In contrast to the standard POSIX popen() function which invokes the shell we
// manage things directly. This is particularly important when running under a 
// server that may remap the shell to /dev/null for security reasons or limit
// execution to an inaccessible "safe directory". 
//

#include "platform.h"


#if defined(_MSDOS) || defined(_WIN32)
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

FILE * _IB_popen(const char *command, const char *type)
{
  return popen(command, type);
}


extern void _IB_WarningMessage(const char *);

FILE *_IB_popen(const char * const argv[], const char *type)
{
  int  maxlength = 4096;
  char buffer[4096];
  int  length = 0;

  buffer[0] = '\0';
  for (int i=0; argv[i]; i++)
    {
      size_t len = strlen(argv[i]);
      if ((length += 4 + len) >= maxlength)
	{
	  _IB_WarningMessage("Popen args too long");
	  return NULL;
	}
      if (i) strcat(buffer, " \"");
      strcat(buffer, argv[i]);
      if (i) strcat(buffer, "\"");
    }
  return popen(buffer, type);
}

int _IB_pclose(FILE *iop, int Zombie)
{
  return pclose(iop);
}

#else

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "common.hxx"
#include "string.hxx"


#ifdef __GNUG__
# undef NO_ALLOCA
#else
# ifndef NO_ALLOCA_H
#  include <alloca.h>
# endif
#endif

extern "C" {
 int sigblock(int mask);
 //int sigmask(int signum);
 int sigpause(int mask);
 int sigsetmask(int mask);
} ;

extern void _IB_WarningMessage(const char *);
extern void _IB_PanicMessage(const char *);

#ifndef FORK
# if defined(sun) || defined(__ultrix) || defined(__bsdi__)
#  define FORK vfork
# else
#  define FORK fork
# endif
#endif

#define	MAXGLOBARGS	1024

/*
 * Special version of popen which avoids call to shell.  This ensures noone
 * may create a pipe to a hidden program as a side effect of a list or dir
 * command.
 */
static struct process_table {
  int   pid;
  char *command; 
} *process_table; 

static int fds;

extern FILE *_IB_popen(const char * const argv[], const char *type);

static void process_table_cleanup()
{
  if (process_table == NULL)
    return;

  for (int i=0; i < fds; i++)
    {
      if (process_table[i].command)
	{
	  free(process_table[i].command);
	  process_table[i].command = NULL;
	}
    }
  free (process_table);
  process_table = NULL;
}


static inline char *priv_slash_fixup(char *dest, const char *src)
{
  char *p = dest;
  while ((*dest = *src++) != '\0')
  if (*dest != '\\' || *src == '\\')
    dest++;
  return p;
}

FILE *_IB_popen(const char *Command, const char *type)
{
  FILE *fp = NULL;
  char *argv[MAXGLOBARGS];
  int   argc = 0;

  if (Command == NULL) return NULL;

  while (isspace(*Command) || *Command == ';') Command++;

#ifdef NO_ALLOCA
  char       cmd[4098];
#else
  size_t      len = strlen(Command)+1;
# ifdef __GNUG__
  char        cmd[len];
# else
  char       *cmd = (char *)alloca(len);
  if (cmd) 
#endif 
   memcpy(cmd, Command, len);
#endif

  char *tcp = (char *)cmd;
  int   quote = 0;
  int   need_fixup = 0;

  if (tcp == NULL)
    return fp;
 
  do { 
    argv[argc++] = tcp;

    if (argc >= (MAXGLOBARGS-1))
      {
	_IB_PanicMessage("_IB_popen: too many arguments. Truncated.");
	break;
      }

    for (;*++tcp;)
      {
	if (quote)
	  {
	    if (*tcp == quote)
	      quote = 0;
	  }
	else if (*tcp == '\\')
	  {
	    need_fixup++;
	    if (*++tcp == '\0')
	      break;
	  }
	else if (*tcp == '"' || *tcp == '\'')
	  quote = *tcp;
	else if (isspace(*tcp))
	  {
	    do {*tcp++ = '\0'; } while (isspace(*tcp));
	    break; // Break out
	  }
      } // for(;;)
  } while (*tcp);

  if (need_fixup)
    {
#ifdef __GNUG__
     char *gargv[argc+1];
#else
# ifdef NO_ALLOCA
      char *gargv[MAXGLOBARGS];
# else
      char **gargv = (char **)alloca((argc+1)*sizeof(char *));
      if (gargv == NULL)
	{
	  _IB_PanicMessage("_IB_popen can't allocate from stack.");
	  return fp; // Can't continue
	}
# endif
#endif

      for (int i=0; i < argc; i++)
	gargv[i] = priv_slash_fixup(new char[strlen(argv[i])+1], argv[i]);
      gargv[argc] = NULL;
  
      fp = _IB_popen(gargv, type);

      for (int j=0; j< argc; j++)
	delete[] gargv[j];
    }
  else
    {
      argv[argc] = NULL;
      fp = _IB_popen(argv, type);
    }

#ifdef NO_ALLOCA
  delete[] cmd;
#endif
  return fp;
}


FILE *_IB_popen(const char * const argv[], const char *type)
{
	char *cp;
	FILE *iop;
	pid_t    pdes[2], pid;
	char **pop;

	if (((*type != 'r') && (*type != 'w')) || type[1])
		return (NULL);

	if (!process_table) {
		if ((fds = getdtablesize()) <= 0)
			return (NULL);
		if ((process_table = (struct process_table *)
			malloc((u_int)(fds * sizeof(struct process_table)))) == NULL)
			return (NULL);
		memset(process_table, 0, fds * sizeof(struct process_table));
		atexit(process_table_cleanup);

	}
	if (pipe(pdes) < 0)
		return (NULL);

	iop = NULL;

	pid = FORK ();

	switch(pid) {
	case -1:			/* error */
		(void)close(pdes[0]);
		(void)close(pdes[1]);
		return iop;
		/* NOTREACHED */
	case 0:				/* child */
		if (*type == 'r') {
			if (pdes[1] != STDOUT_FILENO) {
				dup2(pdes[1], STDOUT_FILENO);
				(void)close(pdes[1]);
			}
			dup2(STDOUT_FILENO, STDERR_FILENO); /* stderr too! */
			(void)close(pdes[0]);
		} else {
			if (pdes[0] != STDIN_FILENO) {
				dup2(pdes[0], STDIN_FILENO);
				(void)close(pdes[0]);
			}
			(void)close(pdes[1]);
		}
#if 0
		{
		   const struct itimerval in, out;
		   if (setitimer(ITIMER_VIRTUAL, &in, &out)) {
		   }
		}
#endif
		execvp(argv[0], (char *const *)argv); // Search PATH too
		_exit(1);
	}

	/* parent; assume fdopen can't fail...  */
	if (*type == 'r') {
		iop = fdopen(pdes[0], type);
		(void)close(pdes[1]);
	} else {
		iop = fdopen(pdes[1], type);
		(void)close(pdes[0]);
	}

	int fdes    = fileno(iop);

	if (fdes >= fds)
	  {
	    _IB_PanicMessage("File handle in _IB_popen exceeds table size!");
	    fclose(iop);
	    kill(pid, SIGKILL);
	    return NULL;
	  }

	process_table[fdes].pid  = pid;
	if ( process_table[fdes].command )
	  free ( process_table[fdes].command );
	process_table[fdes].command = strdup( argv[0] );

	return (iop);
}

int _IB_pclose(FILE *iop, int Zombie)
{
	auto Warn = [](const struct process_table& process, const char* state)
	{ STRING text;
  	  text << "Sub-Process '" << (process.command ? process.command : "(unknown)") << "'[PID="
		<< process.pid << "] " << (state ? state : "");
	  _IB_WarningMessage(text.c_str());
	};

	int fdes, omask, status, runtime;
	pid_t pid;
//	const char message[] = "Sub-Process '%s'[PID=%d] %s";
// #define Warn(x,s) {char tmp[64]; sprintf(tmp, message, x.command, x.pid, s); _IB_WarningMessage(tmp); }

	/*
	 * pclose returns -1 if stream is not associated with a
	 * `popened' command, or, if already `pclosed'.
	 */
	if (process_table == 0 || process_table[fdes = fileno(iop)].pid == 0)
		return (-1);
	(void)fclose(iop);

#ifndef sigmask
# define sigmask(n)              ((unsigned int)1 << (((n) - 1) & (32 - 1)))
#endif

	// omask = sigblock(sigmask(SIGINT)|sigmask(SIGQUIT)|sigmask(SIGHUP));


	if (Zombie == 0)
	   {
	      while ((pid = waitpid(process_table[fdes].pid, &status, 0)) < 0)
		continue;
	   }
	else
	  {
	    int i;
	    int max_tries = (Zombie > 1 ? Zombie : 20);

	    for (i=0; i < max_tries; i++) {
		while ((pid = waitpid(process_table[fdes].pid, &status, WNOHANG)) < 0)
			continue;
		if (Zombie < 0)
		   break;
		if (pid == 0) {
		  if (i >= 1) {
		    if (i == 1) Warn(process_table[fdes], "wayward? Will try now to kill it..");
		    kill(process_table[fdes].pid, SIGKILL);
		  }
		} else if (pid > 0)
		  break;
		if (i != 1)
		  sleep(1);
	    }
	    if (i >= max_tries && pid > 0) Warn(process_table[fdes], "hung?");
	  }
//	(void)sigsetmask(omask);
	process_table[fdes].pid = 0;
	free ( process_table[fdes].command);
	process_table[fdes].command = NULL;
	if (pid < 0)
		return (pid);
	if (WIFEXITED(status))
		return (WEXITSTATUS(status));
	return (1);
}


/*
 * Run argv[0] directly without a shell.
 *
 * input/input_size are written to the program's stdin.
 * stdout and stderr are copied to output.
 *
 * Returns:
 *   child exit status
 *   -1 for a local launch or I/O failure
 *
 * output_size receives the number of bytes written to output.
 */
int _IB_run_filter( const char * const argv[], const void *input, size_t input_size, FILE *output, size_t *output_size)
{
  if (output_size)
    *output_size = 0;

  if (argv == NULL || argv[0] == NULL || output == NULL || (input == NULL && input_size != 0))
    {
      errno = EINVAL;
      return -1;
    }

  int input_pipe[2];
  int output_pipe[2];

  if (pipe(input_pipe) < 0)
    return -1;

  if (pipe(output_pipe) < 0)
    {
      const int saved_errno = errno;

      close(input_pipe[0]);
      close(input_pipe[1]);

      errno = saved_errno;
      return -1;
    }

  const pid_t child = fork();

  if (child < 0)
    {
      const int saved_errno = errno;

      close(input_pipe[0]);
      close(input_pipe[1]);
      close(output_pipe[0]);
      close(output_pipe[1]);

      errno = saved_errno;
      return -1;
    }

  if (child == 0)
    {
      /*
       * Filter stdin.
       */
      if (input_pipe[0] != STDIN_FILENO)
        {
          if (dup2(input_pipe[0], STDIN_FILENO) < 0)
            _exit(126);
        }

      /*
       * Filter stdout.
       */
      if (output_pipe[1] != STDOUT_FILENO)
        {
          if (dup2(output_pipe[1], STDOUT_FILENO) < 0)
            _exit(126);
        }

      /*
       * Preserve the existing _IB_popen("r") behaviour:
       * stderr is merged into stdout.
       */
      if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0)
        _exit(126);

      /*
       * Avoid closing a descriptor if it has become one of
       * stdin, stdout, or stderr.
       */
      if (input_pipe[0] > STDERR_FILENO)
        close(input_pipe[0]);

      if (input_pipe[1] > STDERR_FILENO)
        close(input_pipe[1]);

      if (output_pipe[0] > STDERR_FILENO)
        close(output_pipe[0]);

      if (output_pipe[1] > STDERR_FILENO)
        close(output_pipe[1]);

      execvp(argv[0], (char * const *)argv);

      /*
       * Conventionally means executable not found.
       */
      _exit(127);
    }

  /*
   * Parent does not use the child's pipe ends.
   */
  close(input_pipe[0]);
  close(output_pipe[1]);

  /*
   * Use a separate input-pump process.
   *
   * The main process must continuously drain stdout while input is
   * being written. Otherwise the filter can deadlock when both pipes
   * become full.
   */
  const pid_t writer = fork();

  if (writer < 0)
    {
      const int saved_errno = errno;

      close(input_pipe[1]);
      close(output_pipe[0]);

      kill(child, SIGKILL);

      while (waitpid(child, NULL, 0) < 0 && errno == EINTR)
        ;

      errno = saved_errno;
      return -1;
    }

  if (writer == 0)
    {
      close(output_pipe[0]);

      const unsigned char *data =
          (const unsigned char *)input;

      size_t remaining = input_size;

      while (remaining != 0)
        {
          const size_t chunk = remaining > 65536 ? 65536 : remaining;

          const ssize_t written = write(input_pipe[1], data, chunk);

          if (written > 0)
            {
              data += written;
              remaining -= (size_t)written;
            }
          else if (written < 0 && errno == EINTR)
            {
              continue;
            }
          else
            {
              close(input_pipe[1]);
              _exit(1);
            }
        }

      /*
       * This sends EOF to the filter's stdin.
       */
      close(input_pipe[1]);
      _exit(0);
    }

  /*
   * Only the writer process retains the input pipe.
   */
  close(input_pipe[1]);

  unsigned char buffer[32768];
  size_t total = 0;
  int io_errno = 0;

  for (;;)
    {
      const ssize_t received =
          read(output_pipe[0], buffer, sizeof(buffer));

      if (received > 0)
        {
          if (io_errno == 0)
            {
              size_t done = 0;

              while (done < (size_t)received)
                {
                  const size_t amount = fwrite( buffer + done, 1, (size_t)received - done, output);

                  if (amount == 0)
                    {
                      io_errno = errno ? errno : EIO;
                      break;
                    }

                  done += amount;
                  total += amount;
                }
            }

          /*
           * Continue draining stdout after an output error.
           * Otherwise the filter could remain blocked forever.
           */
        }
      else if (received == 0)
        {
          break;
        }
      else if (errno == EINTR)
        {
          continue;
        }
      else
        {
          io_errno = errno;
          break;
        }
    }

  close(output_pipe[0]);

  int child_status = 0;
  int writer_status = 0;
  pid_t waited;

  do
    {
      waited = waitpid(child, &child_status, 0);
    }
  while (waited < 0 && errno == EINTR);

  if (waited < 0 && io_errno == 0)
    io_errno = errno;

  do
    {
      waited = waitpid(writer, &writer_status, 0);
    }
  while (waited < 0 && errno == EINTR);

  if (waited < 0 && io_errno == 0)
    io_errno = errno;

  if (output_size)
    *output_size = total;

  if (io_errno != 0)
    {
      errno = io_errno;
      return -1;
    }

  int exit_status = 1;

  if (WIFEXITED(child_status))
    exit_status = WEXITSTATUS(child_status);

  /*
   * Prefer the filter's own error status.
   *
   * If the filter says it succeeded but the input writer failed,
   * report an input-pipe error.
   */
  if (exit_status == 0 &&
      (!WIFEXITED(writer_status) ||
       WEXITSTATUS(writer_status) != 0))
    {
      errno = EPIPE;
      return -1;
    }

  return exit_status;
}
#endif
