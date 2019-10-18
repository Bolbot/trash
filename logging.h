#ifndef __LOGGING_H__
#define __LOGGING_H__

#include <mutex>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>

#include <cstring>
#include <ctime>

extern std::mutex cerr_mutex;

#define LOG_CERROR(x) log_errno((__func__), (__FILE__), (__LINE__), (x))

void log_errno(const char *function, const char *file, size_t line, const char *message, int actual_errno = errno) noexcept;

extern std::string server_ip;
extern std::string server_port;
extern std::string server_directory;

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

time_t current_time_t() noexcept;

std::string time_t_to_string(time_t seconds_since_epoch);

#endif
