#ifndef __UTILS_H__
#define __UTILS_H__

#include <string>
#include <iostream>
#include <exception>
#include <fstream>
#include <ctime>
#include <mutex>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <boost/program_options.hpp>

#include "defines.h"
#include "multithreading.h"

extern std::mutex cerr_mutex;
#define LOG_CERROR(x) log_errno((__func__), (__FILE__), (__LINE__), (x))
void log_errno(const char *function, const char *file, size_t line, const char *message) noexcept;

extern std::string server_ip;
extern std::string server_port;
extern std::string server_directory;

void parse_program_options(int argc, char **argv) noexcept;

void daemonize() noexcept;

class log_redirector final
{
private:
	static constexpr char log_file_out_name[] = "the_server_out.log";
 	static constexpr char log_file_err_name[] = "the_server_err.log";
	static constexpr char log_file_log_name[] = "the_server_log.log";

	class redirected_stream final
	{
	private:
		std::ostream *log_stream;
		std::streambuf *old_buffer;
		std::ofstream own_ofstream;
	public:
		redirected_stream(std::ostream &where_from, const char *dest_file)
			: log_stream{ &where_from }, old_buffer{ log_stream->rdbuf() }
		{
			own_ofstream.open(dest_file);
			if (!own_ofstream.is_open())
				throw std::runtime_error("Failed to open file for logging");

			log_stream->rdbuf(own_ofstream.rdbuf());
		}

		~redirected_stream()
		{
			log_stream->rdbuf(old_buffer);
		}
	};

	redirected_stream *redirected_cout = nullptr;
	redirected_stream *redirected_cerr = nullptr;
	redirected_stream *redirected_clog = nullptr;

protected:
	log_redirector() :
		redirected_cout{ new redirected_stream(std::cout, log_file_out_name) },
		redirected_cerr{ new redirected_stream(std::cerr, log_file_err_name) },
		redirected_clog{ new redirected_stream(std::clog, log_file_log_name) }
	{
	}
public:
	static log_redirector &instance()
	{
		static log_redirector object;
		return object;
	}
	log_redirector(const log_redirector &) = delete;
	log_redirector &operator=(const log_redirector &) = delete;
	~log_redirector()
	{
		delete redirected_cout;
		delete redirected_cerr;
		delete redirected_clog;
	}
};

void signal_handler(int signal_number) noexcept;

void set_signal(int signal_number) noexcept;

void set_signals() noexcept;

time_t current_time_t() noexcept;

std::string time_t_to_string(time_t seconds_since_epoch);

size_t set_maximal_avaliable_limit_of_fd() noexcept;

void checked_pclose(FILE *closable) noexcept;

std::string popen_reader(const char *command);

class open_file final
{
/*
*	Abstraction of an open file. Stores file address. RAII file descriptor. Gets some properties in nested class.
*	Problem is the exceptrion propagation and error handling.
*/

private:
	std::string address;
	int fd;

	class file_properties
	{
	private:
		size_t size;
		std::string mime_type;
		std::string last_modified;
	public:
		file_properties(const char *path)
		{
			struct stat statbuf;
			if (stat(path, &statbuf) == -1)
			{
				std::lock_guard<std::mutex> lock(cerr_mutex);
				LOG_CERROR("error of stat, file_properties will remain empty values");
				return;
			}

			size = statbuf.st_size;

			std::string command = "file ";
			command += path;
			command += " --brief --mime";
			mime_type = popen_reader(command.data());
			if (mime_type.back() == '\n')
			{
				mime_type.pop_back();
			}

			time_t last_modified_seconds_since_epoch = statbuf.st_mtim.tv_sec;
			last_modified = time_t_to_string(last_modified_seconds_since_epoch);
		}

		size_t get_size() const noexcept
		{
			return size;
		}
		std::string get_mime_type() const
		{
			return mime_type;
		}
		std::string get_last_modified() const
		{
			return last_modified;
		}
	};

	std::unique_ptr<file_properties> properties{ nullptr };
	bool get_file_properties() noexcept
	{
		try
		{
			properties.reset(new file_properties(address.data()));
		}
		catch (std::exception &e)
		{
			std::lock_guard<std::mutex> lock(cerr_mutex);
			std::cerr << "Failed to get properties of the file "
				<< address << ": " << e.what() << "\n";
			return false;
		}
		catch (...)
		{
			std::lock_guard<std::mutex> lock(cerr_mutex);
			std::cerr << "Unknown error while getting properties of file " << address << "\n";
			return false;
		}
		return true;
	}
public:
	open_file(const char *path) : address{ path }, fd{ open(path, O_RDONLY) }
	{}

	open_file(const open_file &) = delete;
	open_file &operator=(const open_file &) = delete;

	~open_file()
	{
		if (fd == -1)
		{
			return;
		}
		if (close(fd) == -1)
		{
			std::lock_guard<std::mutex> lock(cerr_mutex);
			LOG_CERROR("failed to close the opened file");
			std::cerr << "File with descriptor " << fd << " wasn't properly closed.\n";
		}
	}

	operator int() const noexcept
	{
		return fd;
	}

	explicit operator bool() const noexcept
	{
		return (fd != -1);
	}

	size_t size()
	{
		if (fd == -1)
		{
			return 0;
		}

		if (!properties)
		{
			if (!get_file_properties())
			{
				return 0;
			}
		}
		return properties->get_size();
	}

	std::string mime_type()
	{
		if (fd == -1)
		{
			return "";
		}

		if (!properties)
		{
			if (!get_file_properties())
			{
				return "";
			}
		}
		return properties->get_mime_type();
	}

	std::string last_modified()
	{
		if (fd == -1)
		{
			return "";
		}

		if (!properties)
		{
			if (!get_file_properties())
			{
				return "";
			}
		}
		return properties->get_last_modified();
	}

	std::string location() const
	{
		return address;
	}
};

time_t current_time_t() noexcept;

std::string time_t_to_string(time_t seconds_since_epoch);

void atexit_terminator() noexcept;

[[noreturn]] void terminate_handler() noexcept;

#endif
