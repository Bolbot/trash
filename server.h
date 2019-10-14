#ifndef __SERVER_H__
#define __SERVER_H__

#include <iostream>
#include <thread>
#include <mutex>
#include <regex>

#include "utils.h"

struct addrinfo get_addrinfo_hints() noexcept;

int get_binded_socket(struct addrinfo *address_info) noexcept;

int get_listening_socket() noexcept;

void run_server_loop(int master_socket);

class active_connection final
{
	class implementation final
	{
	private:
		int fd;
	public:
		explicit implementation(int master_socket) noexcept : fd{ accept(master_socket, nullptr, nullptr) }
		{
			if (fd == -1)
			{
				std::lock_guard<std::mutex> lock(cerr_mutex);
				LOG_CERROR("Error of accept, connection stays flawed");
			}
		}
		~implementation()
		{
			if (fd == -1)
			{
				return;
			}

			if (close(fd) == -1)
			{
				std::lock_guard<std::mutex> lock(cerr_mutex);
				LOG_CERROR("Failed to close connection");
				std::cerr << "fd " << fd << " not closed in proper way\n";
			}
		}

		implementation(const implementation &) = default;
		implementation &operator=(const implementation &) = default;
		implementation(implementation &&) = default;
		implementation &operator=(implementation &&) = default;

		operator int() const noexcept
		{
			return fd;
		}
	};
private:
	std::shared_ptr<implementation> fd;
public:
	explicit active_connection(int master_socket) : fd{ new implementation(master_socket) }
	{}

	active_connection(const active_connection &other) : fd{ other.fd }
	{}
	active_connection &operator=(const active_connection &other)
	{
		if (this != &other)
		{
			fd = other.fd;
		}
		return *this;
	}

	active_connection(active_connection &&other) noexcept : fd{ std::move(other.fd) }
	{}
	active_connection &operator=(active_connection &&other) noexcept
	{
		if (&other != this)
			fd = std::move(other.fd);
		return *this;
	}

	~active_connection()
	{}

	explicit operator bool() const noexcept
	{
		return (*fd != -1);
	}

	operator int() const noexcept
	{
		return *fd;
	}
};

void process_the_accepted_connection(active_connection client_fd);

class http_request final
{
private:
	std::string source;
	std::string address;
	short status = 520;
	char delimiter;
	bool http09;

	const std::regex simple_request
	{
		R"(^GET\s\S+$)"
	};
	const std::regex full_request
	{
		R"(^((GET)|(POST)|(HEAD))(\s\S+\s)(HTTP/\d\.\d)$)"
	};
	const std::regex header
	{
		R"(^[^()<>@,;:\"/\[\]?={} 	[:cntrl:]]+:[^[:cntrl:]]*$)"
	};

	std::string readline(std::istringstream &stream) const
	{
		std::string result;
		getline(stream, result, delimiter);
		if (delimiter == '\r')
		{
			if (stream.peek() == '\n')
				stream.get();
		}
		return result;
	}
	void set_delimiter() noexcept
	{
		if (source.find('\r') != std::string::npos)
			delimiter = '\r';
		else
			delimiter = '\n';
	}
	bool is_invalid_request() noexcept
	{
		if (source.find('\n') == std::string::npos && source.find('\r') == std::string::npos)
		{
			if (source.size())
				status = 414;
			else
				status = 400;
			return true;
		}
		return false;
	}
	void set_address_from_first_line(std::string first_line)
	{
		std::stringstream temp;
		temp.str(first_line);
		temp >> address;
		temp >> address;
		auto question_mark_pos = address.find("?");
		if (question_mark_pos != std::string::npos)
		{
			address.erase(question_mark_pos);
		}
	}
public:
	explicit http_request(const char *s) : source{ s }
	{
		set_delimiter();
	}

	http_request(const http_request &) = default;				// is this a problem? do some unit tests perhaps...
	http_request &operator=(const http_request &) = default;

	void parse_request()
	{
		if (is_invalid_request())
		{
			return;
		}

		std::istringstream stream{ source };

		std::string first_line = readline(stream);
		if (first_line.size() < 5 || first_line.find(" ") == std::string::npos)
		{
			status = 400;
			return;
		}

		if (regex_match(first_line, full_request))
		{
			http09 = false;
			if (first_line.find(" HTTP/0.9") != std::string::npos)
			{
				http09 = true;
			}
			else
			{
				if (first_line.find(" HTTP/1.0") == std::string::npos)
				{
					status = 505;
					return;
				}
				if (first_line.find("HEAD") == 0 || first_line.find("POST") == 0)		// why not implement HEAD sometime later?
				{
					status = 405;
					return;
				}
			}
		}
		else if(regex_match(first_line, simple_request))
		{
			http09 = true;
		}
		else
		{
			status = 400;
			return;
		}

		status = 200;

		set_address_from_first_line(first_line);

		if (http09)
			return;

		std::string current;
		while (!(current = readline(stream)).empty())
		{
			if (!regex_match(current, header))
			{
				std::cout << "Found improper header in request: " << current << std::endl;
			}
		}
	}
	explicit operator bool() const noexcept
	{
		return (status == 200);
	}
	short get_status() const noexcept
	{
		return status;
	}
	std::string get_address() const noexcept
	{
		return address;
	}
	bool status_required() const noexcept
	{
		return !http09;
	}
};

void process_client_request(active_connection &client, http_request request);

const char *http_response_phrase(short status) noexcept;

ssize_t send_status_line(active_connection &client, short status);

ssize_t send_headers(active_connection &client, open_file &file);

void send_client_a_file(active_connection &client, open_file &file) noexcept;

#endif
