#include "file_wrapper.h"

void checked_pclose(FILE *closable) noexcept
{
	if (pclose(closable) == -1)
	{
		{
			std::lock_guard<std::mutex> lock(cerr_mutex);
			LOG_CERROR("failed to pclose the popened file");
		}

		int descriptor = fileno(closable);
		if (descriptor != -1)
		{
			std::lock_guard<std::mutex> lock(cerr_mutex);
			std::cerr << "File with descriptor " << descriptor << " wasn't pclosed in proper way.\n";
		}
	}
}

std::string popen_reader(const char *command)
{
//	using FILE_pointer = std::unique_ptr<FILE, void (*)(FILE *)>;

//	FILE_pointer source = FILE_pointer(popen(command, "r"), &checked_pclose);

	FILE *source = popen(command, "r");

	if (!source)
	{
		checked_pclose(source);
		std::lock_guard<std::mutex> lock(cerr_mutex);
		LOG_CERROR("failed to popen the file");
		return std::string{};
	}

	constexpr size_t buffer_size = 1024;
	char buffer[buffer_size];

	rewind(source/*.get()*/);
	if (!fgets(buffer, buffer_size, source/*.get()*/))
	{
		checked_pclose(source);
		std::lock_guard<std::mutex> lock(cerr_mutex);
		LOG_CERROR("fgets failed so popen_reader returns \"\" (empty result)");
		return std::string{};
	}

	checked_pclose(source);

	return std::string{ buffer };
}
