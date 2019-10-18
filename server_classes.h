#ifndef __SERVER_CLASSES_H__
#define __SERVER_CLASSES_H__

#include <memory>
#include <string>
#include <regex>
#include <sstream>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "logging.h"

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

	active_connection() : fd{ nullptr }
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
		return (fd && (*fd != -1));
	}

	operator int() const noexcept
	{
		return ((fd) ? *fd : -1);
	}
};

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

void process_the_accepted_connection(active_connection client_fd);

namespace concrete
{
#include <atomic>
#include <exception>
#include <thread>
#include <future>
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>

#include <iostream>

	class mt_safe_queue final
	{
	private:
		std::queue<std::shared_ptr<active_connection>> queue;
		mutable std::mutex mutex;
		std::condition_variable condv;
	public:
		mt_safe_queue() = default;
		mt_safe_queue(const mt_safe_queue &) = delete;
		mt_safe_queue &operator=(const mt_safe_queue &) = delete;

		void push(active_connection &&element)
		{
			std::shared_ptr<active_connection> pointer = std::make_shared<active_connection>(std::move(element));

			std::lock_guard<std::mutex> lock(mutex);
			queue.push(std::move(pointer));
		}

		bool try_pop(active_connection &dest)
		{
			std::lock_guard<std::mutex> lock(mutex);

			if (queue.empty())
				return false;

			dest = std::move(*queue.front());
			queue.pop();
			return true;
		}

		std::shared_ptr<active_connection> try_pop()
		{
			std::lock_guard<std::mutex> lock(mutex);

			if (queue.empty())
				return nullptr;

			std::shared_ptr<active_connection> pointer = std::move(queue.front());
			queue.pop();
			return pointer;
		}

		void wait_and_pop(active_connection &dest)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (queue.empty())
				condv.wait(lock, [this]() { return !queue.empty(); });

			dest = std::move(*queue.front());
			queue.pop();
		}

		std::shared_ptr<active_connection> wait_and_pop()
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (queue.empty())
				condv.wait(lock, [this]() { return !queue.empty(); });

			std::shared_ptr<active_connection> pointer = std::move(queue.front());
			queue.pop();
			return pointer;
		}

		bool empty() const
		{
			std::lock_guard<std::mutex> lock(mutex);
			return queue.empty();
		}
	};

	class stealing_queue final
	{
	private:
		std::deque<std::shared_ptr<active_connection>> deque;
		mutable std::mutex mutex;
		std::condition_variable condv;
	public:
		stealing_queue() = default;
		stealing_queue(const stealing_queue &) = delete;
		stealing_queue &operator=(const stealing_queue &) = delete;

		void push(active_connection &&element)
		{
			std::shared_ptr<active_connection> ptr = std::make_shared<active_connection>(std::move(element));

			std::lock_guard<std::mutex> lock(mutex);
			deque.push_front(std::move(ptr));
			condv.notify_one();
		}

		bool try_pop(active_connection &dest)
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (deque.empty())
				return false;

			dest = std::move(*deque.front());
			deque.pop_front();
			return true;
		}
		std::shared_ptr<active_connection> try_pop()
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (deque.empty())
				return nullptr;

			std::shared_ptr<active_connection> ptr{ std::move(deque.front()) };
			deque.pop_front();
			return ptr;
		}
		void wait_and_pop(active_connection &dest)
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (deque.empty())
				condv.wait(lock, [this]() { return !deque.empty(); });

			dest = std::move(*deque.front());
			deque.pop_front();
		}
		std::shared_ptr<active_connection> wait_and_pop()
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (deque.empty())
				condv.wait(lock, [this]() { return !deque.empty(); });

			std::shared_ptr<active_connection> ptr{ std::move(deque.front()) };
			deque.pop_front();
			return ptr;
		}

		bool try_steal(active_connection &dest)
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (deque.empty())
				return false;

			dest = std::move(*deque.back());
			deque.pop_back();
			return true;
		}
		std::shared_ptr<active_connection> try_steal()
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (deque.empty())
				return nullptr;

			std::shared_ptr<active_connection> ptr = std::move(deque.back());
			deque.pop_back();
			return ptr;
		}

		bool empty() const
		{
			std::lock_guard<std::mutex> lock(mutex);
			return deque.empty();
		}	
	};

	class thread_joiner final
	{
	private:
		std::vector<std::thread> &threads;
	public:
		explicit thread_joiner(std::vector<std::thread> &t): threads(t)
		{}
		~thread_joiner()
		{
			for (auto &i: threads)
				if (i.joinable())
					i.join();
		}
	};

	class thread_pool final
	{
	private:
		std::atomic<bool> terminate_flag;
		mt_safe_queue common_tasks_queue;
		std::vector<std::unique_ptr<stealing_queue>> task_queues;
		static thread_local stealing_queue *local_tasks_queue;
		static thread_local size_t thread_index;

		std::vector<std::thread> threads;
		thread_joiner joiner_of_pool_threads;

		bool try_steal(active_connection &dest)
		{
			for (size_t i = 0; i != task_queues.size(); ++i)
			{
				size_t index = (thread_index + i + 1) % task_queues.size();

				if (task_queues[index] && task_queues[index]->try_steal(dest))
					return true;
			}

			return false;
		}

		void working_loop(size_t index)
		{
			thread_index = index;
			local_tasks_queue = task_queues[thread_index].get();

			while (!terminate_flag.load())
			{
				active_connection connection;

				if ((local_tasks_queue && local_tasks_queue->try_pop(connection))
					|| common_tasks_queue.try_pop(connection) || try_steal(connection))
				{
					try
					{
						process_the_accepted_connection(std::move(connection));
					}
					catch (std::exception &e)
					{
						std::cerr << std::this_thread::get_id() << " got an exception: " << e.what() << std::endl;
					}
					catch (...)
					{
						std::cerr << std::this_thread::get_id() << " got unknown exception thrown" << std::endl;
					}
				}
				else
					std::this_thread::yield();
			}
		}

	public:
		thread_pool() : terminate_flag{ false },
				task_queues(std::thread::hardware_concurrency() - 1),
				threads(std::thread::hardware_concurrency() - 1),
				joiner_of_pool_threads{ threads }
		{
			try
			{
				for (auto &i: task_queues)
					i.reset(new stealing_queue);

				for (size_t i = 0; i != threads.size(); ++i)
					threads[i] = std::thread(&thread_pool::working_loop, this, i);
			}
			catch (...)
			{
				terminate_flag.store(true, std::memory_order_release);
				std::cerr << "thread pool initialization failed" << std::endl;
			}
		}
		~thread_pool()
		{
			terminate_flag.store(true, std::memory_order_release);
		}

		void enqueue_task(active_connection &&connection)
		{
			if (local_tasks_queue)
				local_tasks_queue->push(std::move(connection));
			else
				common_tasks_queue.push(std::move(connection));
		}
	};
}

using namespace concrete;

#endif
