#ifndef __FILE_WRAPPER_H__
#define __FILE_WRAPPER_H__

#include <string>
#include <memory>

#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "logging.h"

void checked_pclose(FILE *closable) noexcept;

std::string popen_reader(const char *command);

class open_file final
{
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


#endif
