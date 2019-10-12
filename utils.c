#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>

#include "defines.h"
#include "utils.h"

FILE *_myoutput;

size_t get_file_size(const char *fpath)
{
	size_t res = 0;

	struct stat sta;
	if(stat(fpath, &sta) == -1) fprintf(_myoutput, "Fail of stat(\"%s\"): %s\n", fpath, strerror(errno));
	else res = sta.st_size;
	if(VERBOSE) fprintf(_myoutput, "Foud out via stat that %s weights %lu bytes.\n", fpath, res);

	return res;
}

int get_file_mime_type(const char *fpath, char *mimetype)
{
	char command[BUFSIZ];	memset(command, 0, BUFSIZ);	sprintf(command, "file --mime-type %s", fpath);
	FILE *mf = popen(command, "r");
	if(mf) fscanf(mf, "%*s %s", mimetype);
	else { fprintf(_myoutput, "Failed to popen to get %s mime-type\n", fpath); return -1; }

	if(pclose(mf) == -1) { fprintf(_myoutput, "Failed to close popened file (fd is %d)\n", fileno(mf)); return -1; }
	if(VERBOSE) fprintf(_myoutput, "Discovered that %s has mime-type %s\n", fpath, mimetype);
	return 0;
}

int prepare_file_to_send(const char *rel_path, size_t *file_size, char *mime_type)
{
	if(!directory) { fprintf(_myoutput, "Directory address is unavailable. Quitting."); exit(EXIT_FAILURE); }

	char faddr[BUFSIZ];	memset(faddr, 0, BUFSIZ);	strncpy(faddr, directory, BUFSIZ);
	strcat(faddr, (*rel_path == '/') ? rel_path + 1 : rel_path);
	for(char *i = faddr; *i; ++i) if(*i == '?') *i = '\0';

	struct stat statbuf;
	if(stat(faddr, &statbuf) == -1) { fprintf(_myoutput, "File '%s' does not exist\n", faddr); return -1; }

	*file_size = get_file_size(faddr);
	if(get_file_mime_type(faddr, mime_type) == -1) sprintf(mime_type, "text/html; charset-utf-8");

	if(VERBOSE) fprintf(_myoutput, "Preparing descriptor of file '%s' (%lu bytes)\n", faddr, *file_size);
	if(*file_size == 0) { fprintf(_myoutput, "Looks like there actually is no such file. Returning -1."); return -1; }

	int fd = open(faddr, 0);
	if(fd == -1) { fprintf(_myoutput, "Failed to open file '%s'\n", faddr); return -1; }
	if(VERBOSE) fprintf(_myoutput, "Opened file at descriptor %d, don't forget to close after things're done\n", fd);
	return fd;
}


FILE *redirect_output(const char *file_addr)
{
	FILE *output = NULL;
	if(!file_addr || !(output = fopen(file_addr, "w")))
	{
		perror("Failed to use file specified for output. Will output to stderr\n");
		_myoutput = stderr;
		if(close(STDOUT_FILENO) == -1) perror("Didn't close stdout fd");
		if(dup2(STDERR_FILENO, STDOUT_FILENO)) perror("dup2 stdout to stderr");
	}
	else
	{
		_myoutput = output;
		if(setvbuf(_myoutput, NULL, _IONBF, 0) == -1) perror("failed to setvbuf to logfile");
		if(fflush(NULL)) perror("failed to fflush all output streams\n");

		if(close(STDOUT_FILENO) == -1) fprintf(_myoutput, "Didn't close stdout fd: %s\n", strerror(errno));
		if(dup2(fileno(_myoutput), STDOUT_FILENO) == -1) fprintf(_myoutput, "dup2 stdout to logfile: %s\n", strerror(errno));

		if(close(STDERR_FILENO) == -1) fprintf(_myoutput, "Didn't close stderr fd: %s\n", strerror(errno));
		if(dup2(fileno(_myoutput), STDERR_FILENO) == -1) fprintf(_myoutput, "dup2 stderr to logfile: %s\n", strerror(errno));
	}

	if(close(STDIN_FILENO) == -1) fprintf(_myoutput, "Didn't close stdin fd: %s\n", strerror(errno));
	return _myoutput;
}

void finish()
{
	if(VERBOSE) fprintf(_myoutput, "Server finishes it's work and closing descriptor of output %d\n", fileno(_myoutput));
	if(_myoutput != stdout) fclose(_myoutput);
	sleep(1);
	fprintf(_myoutput, "Server finishes it's work.\n");
	exit(EXIT_SUCCESS);
}

void sig_handler(int code)
{
	if(VERBOSE) fprintf(_myoutput, "\t[Got signal: %s]\n", strsignal(code));
	if(code == SIGCHLD)
	{
	//	int status;									// we are multithreaded and don't need this
	//	waitpid(-1, &status, 0);
	//	fprintf(_myoutput, "\tChild terminated: %s\n", strsignal(status));
	}

	if(code == SIGINT || code == SIGTERM || code == SIGQUIT)
	{
		fprintf(_myoutput, "Server is signaled to finish working.\n");
		if(fclose(_myoutput)) perror("Failed to close logs. But who'll read this if stderr was supposed to be closed anyway?");
		exit(EXIT_SUCCESS);
	}
}

void setsignals()
{
	struct sigaction siga;
	siga.sa_handler = sig_handler;
	siga.sa_flags = SA_RESTART;
	sigemptyset(&siga.sa_mask);
	if(sigaction(SIGINT, &siga, 0) == -1) fprintf(_myoutput, "Failed to set hander for SIGINT\n");
	if(sigaction(SIGTERM, &siga, 0) == -1) fprintf(_myoutput, "Failed to set hander for SIGTERM\n");
	if(sigaction(SIGHUP, &siga, 0) == -1) fprintf(_myoutput, "Failed to set hander for SIGHUP\n");
	if(sigaction(SIGCHLD, &siga, 0) == -1) fprintf(_myoutput, "Failed to set hander for SIGCHLD\n");
	if(sigaction(SIGQUIT, &siga, 0) == -1) fprintf(_myoutput, "Failed to set hander for SIGQUIT\n");
	if(sigaction(SIGUSR1, &siga, 0) == -1) fprintf(_myoutput, "Failed to set hander for SIGUSR1\n");
	if(sigaction(SIGUSR2, &siga, 0) == -1) fprintf(_myoutput, "Failed to set hander for SIGUSR2\n");
	if(VERBOSE) fprintf(_myoutput, "Set custom signal handling.\n");
}
