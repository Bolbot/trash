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

#include "utils.h"

void parse_program_options(int argc, char **argv) noexcept
{
	// rather than getopt this version is using boost::program_options
	try
	{
		boost::program_options::options_description options("Call with following obligatory arguments");
		options.add_options()
			("host,h", boost::program_options::value<std::string>(&server_ip), "IP of server (i. e. 127.0.0.1)")
			("port,p", boost::program_options::value<std::string>(&server_port), "Port (use in range 1024..65535)")
			("directory,d", boost::program_options::value<std::string>(&server_directory), "Directory");

		boost::program_options::variables_map map;
		boost::program_options::store(boost::program_options::parse_command_line(argc, argv, options), map);
		boost::program_options::notify(map);

		if (!map.count("host") || !map.count("port") || !map.count("directory"))
		{
			std::lock_guard<std::mutex> lock(cerr_mutex);
			std::cerr << options << "\n";
			exit(EXIT_SUCCESS);
		}
		if (server_ip.empty() || server_port.empty() || server_directory.empty())
			throw std::runtime_error("Failed to parce given comand line arguemnts");
	}
	catch (std::exception &e)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		std::cerr << "Command-line arguments error: " << e.what() << ". Terminating.\n";
		exit(EXIT_FAILURE);	
	}
	catch (...)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		std::cerr << "Unknown error in command-line parser. Terminating.\n";
		exit(EXIT_FAILURE);
	}
}

void daemonize() noexcept
{
	pid_t pid = fork();

	if (pid == -1)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		std::cerr << "Error creating child process in damonize.\n";
		exit(EXIT_FAILURE);
	}
	else if (pid != 0)
	{
		exit(EXIT_SUCCESS);
	}

	umask(0);

	set_signals();

	try
	{
		log_redirector::instance();
	}
	catch (std::exception &e)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		std::cerr << "Failed to redirect output to log files: " << e.what() << "\n";
	}
	catch (...)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		std::cerr << "Unknown error while redirecting output to log files.\n";
	}

	pid_t sid = setsid();

	if (sid == -1)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		LOG_CERROR("program terminates due to the setsid failure");
		exit(EXIT_FAILURE);
	}

	int chdir_res = chdir("/");
	if (chdir_res == -1)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		LOG_CERROR("program terminates due to the chdir failure");
		exit(EXIT_FAILURE);
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	if (std::atexit(atexit_terminator))
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		std::cerr << "Failed to set function for terminating threads at exit: atexit_terminator()\n";
	}
	std::set_terminate(terminate_handler);

	std::clog << "Daemoized successfully. " << time_t_to_string(current_time_t())
		<< "\nMaster process id " << getpid()
		<< "\nServer IP " << server_ip << "\nServer port " << server_port
		<< "\nServer directory " << server_directory << std::endl;
}

size_t get_file_size(const char *fpath)
{
	size_t res = 0;

	struct stat sta;
	if(stat(fpath, &sta) == -1) std::cerr << "Fail of stat(\"" << fpath << "\"): " << strerror(errno) << "\n";
	else res = sta.st_size;
	if(VERBOSE) std::cerr << "Foud out via stat that " << fpath << " weights " << res << " bytes.\n";

	return res;
}

int get_file_mime_type(const char *fpath, char *mimetype)
{
	char command[BUFSIZ];	memset(command, 0, BUFSIZ);	sprintf(command, "file --mime-type %s", fpath);
	FILE *mf = popen(command, "r");
	if(mf) fscanf(mf, "%*s %s", mimetype);
	else { std::cerr << "Failed to popen to get " << fpath << " mime-type\n"; return -1; }

	if(pclose(mf) == -1) { std::cerr << "Failed to close popened file (fd is " << fileno(mf) << ")\n"; return -1; }
	if(VERBOSE) std::cerr << "Discovered that " << fpath << " has mime-type " << mimetype << "\n";
	return 0;
}

int prepare_file_to_send(const char *rel_path, size_t *file_size, char *mime_type)
{
	char faddr[BUFSIZ];	memset(faddr, 0, BUFSIZ);	strncpy(faddr, server_directory.data(), BUFSIZ);
	strcat(faddr, (*rel_path == '/') ? rel_path + 1 : rel_path);
	for(char *i = faddr; *i; ++i) if(*i == '?') *i = '\0';

	struct stat statbuf;
	if(stat(faddr, &statbuf) == -1) { std::cerr << "File '" << faddr << "' does not exist\n"; return -1; }

	*file_size = get_file_size(faddr);
	if(get_file_mime_type(faddr, mime_type) == -1) sprintf(mime_type, "text/html; charset-utf-8");

	if(*file_size == 0) { std::cerr << "Looks like there actually is no such file. Returning -1."; return -1; }

	int fd = open(faddr, 0);
	if(fd == -1) { std::cerr << "Failed to open file '" << faddr << "'\n"; return -1; }
	if(VERBOSE) std::cerr << "Opened file at descriptor " << fd << ", don't forget to close after things're done\n";
	return fd;
}

void signal_handler(int signal_number) noexcept
{
	if (signal_number == SIGINT || signal_number == SIGTERM || signal_number == SIGQUIT || signal_number == SIGABRT)
	{
		std::clog << "Interrupted by signal " << signal_number << ": " << strsignal(signal_number)
			<< "\nFinishing the work and shutting the server.\n";

		exit(EXIT_SUCCESS);
	}
}

void set_signal(int signal_number, struct sigaction &sa) noexcept
{
	if (sigaction(signal_number, &sa, nullptr) == -1)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		LOG_CERROR("sigaction failed");
		std::cerr << "Failed to set handler for " << strsignal(signal_number) << "\n";
	}
}

void set_signals() noexcept
{
	struct sigaction sa;
	sa.sa_handler = signal_handler;
	sa.sa_flags = SA_RESTART | SA_NOCLDWAIT;
	sigemptyset(&sa.sa_mask);
	set_signal(SIGINT, sa);
	set_signal(SIGHUP, sa);
	set_signal(SIGTERM, sa);
	//set_signal(SIGCHLD, sa);	// is this the reason of errno 10 ?
	set_signal(SIGQUIT, sa);
	set_signal(SIGUSR1, sa);
	set_signal(SIGUSR2, sa);
}

size_t set_maximal_avaliable_limit_of_fd() noexcept
{
	struct rlimit descriptors_limit;
	if (getrlimit(RLIMIT_NOFILE, &descriptors_limit) == -1)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		LOG_CERROR("getrlimit failed");
		return 0;
	}

	rlim_t previous = descriptors_limit.rlim_cur;

	descriptors_limit.rlim_cur = descriptors_limit.rlim_max;

	if (setrlimit(RLIMIT_NOFILE, &descriptors_limit) == -1)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		LOG_CERROR("setrlimit failed");
		return previous;
	}

	return descriptors_limit.rlim_cur;
}

void atexit_terminator() noexcept
{
	std::clog << "Exiting. " << time_t_to_string(current_time_t()) << std::endl;
}

[[noreturn]] void terminate_handler() noexcept
{
	std::clog << time_t_to_string(current_time_t()) << std::endl;

	std::exception_ptr current = std::current_exception();
	if (current)
	{
		try
		{
			std::rethrow_exception(current);
		}
		catch (std::exception &e)
		{
			std::lock_guard<std::mutex> lock(cerr_mutex);
			std::cerr << "Terminating because of unhandled exception: " << e.what() << "\n";
		}
		catch (...)
		{
			std::lock_guard<std::mutex> lock(cerr_mutex);
			std::cerr << "Terminating because of unknown exception (not an std::exception)\n";
		}
	}
	else
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		std::cerr << "Terminating because of unhnandled bare throw; or unprovoked call to std::terminate()\n";
	}

	std::abort();
}
