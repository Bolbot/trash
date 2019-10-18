#include "logging.h"

std::mutex cerr_mutex;

std::string server_ip;
std::string server_port;
std::string server_directory;

constexpr char log_redirector::log_file_out_name[];
constexpr char log_redirector::log_file_err_name[];
constexpr char log_redirector::log_file_log_name[];

void log_errno(const char *function, const char *file, size_t line, const char *message, int actual_errno) noexcept
{
	// cerr_mutex is locked before call to this function
	std::cerr << "Error in " << function << " (" << file << ", line " << line << ")\n";

	constexpr size_t buffer_size = 1024;
	static thread_local char buffer[buffer_size] = { 0 };

	std::cerr << "errno " << actual_errno << " means ";

#if (!defined(_GNU_SOURCE) && defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L)

	int strerror_res = strerror_r(actual_errno, buffer, buffer_size);
	if (strerror_res != 0)
	{
		std::cerr << "(failed to decipher because of error with errno " << (strerror_res == -1 ? errno : strerror_res)  << ")\n";
	}
	else
	{
		std::cerr << buffer << "\n";
	}

	std::cout << "using XSI strerror_r, return type is " << typeid(decltype(strerror_r(actual_errno, buffer, buffer_size))).name() << std::endl;

#elif defined(_GNU_SOURCE)

	const char *strerror_res = strerror_r(actual_errno, buffer, buffer_size);
	if (strerror_res)
	{
		std::cerr << strerror_res << "\n";
	}
	else
	{
		std::cerr << "(failed to decipher)\n";
	}

	std::cout << "using GNU strerror_r, return type is " << typeid(decltype(strerror_r(actual_errno, buffer, buffer_size))).name() << std::endl;

#else

	std::cerr << "(alas impossible to report errno-provided errors)\n";

#endif	

	std::cerr << "Therefore " << message << "\n\n";
}

time_t current_time_t() noexcept
{
	return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

std::string time_t_to_string(time_t seconds_since_epoch)
{
	struct tm time_now;
	tzset();
	struct tm *ret_val = localtime_r(&seconds_since_epoch, &time_now);
	if (ret_val != &time_now)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		LOG_CERROR("requested data-string will be empty due to fail of localtime_r");
		return "";
	}

	const char *day_of_week[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	const char *month[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

	constexpr size_t date_max_length = 512;
	char date_string[date_max_length];

	const char format_string[] = ", %d  %Y %T GMT";
	size_t strftime_res = strftime(date_string, date_max_length, format_string, &time_now);
	if (strftime_res == 0)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		LOG_CERROR("requested data-string will be empty due to fail of strftime_res");
		return "";
	}

	std::string result;
	result += day_of_week[time_now.tm_wday];
	result += date_string;
	constexpr size_t month_position = 8;
	result.insert(month_position, month[time_now.tm_mon]);

	return result;
}

