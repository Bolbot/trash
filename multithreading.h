#ifndef __MULTITHREADING_H__
#define __MULTITHREADING_H__

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

template <typename T>
class mt_safe_queue final
{
private:
	std::queue<std::shared_ptr<T>> queue;
	mutable std::mutex mutex;
	std::condition_variable condv;
public:
	mt_safe_queue() = default;
	mt_safe_queue(const mt_safe_queue &) = delete;
	mt_safe_queue &operator=(const mt_safe_queue &) = delete;

	void push(T &&element)
	{
		std::shared_ptr<T> pointer = std::make_shared<T>(std::move(element));

		std::lock_guard<std::mutex> lock(mutex);
		queue.push(std::move(pointer));
	}

	bool try_pop(T &dest)
	{
		std::lock_guard<std::mutex> lock(mutex);

		if (queue.empty())
			return false;

		dest = std::move(*queue.front());
		queue.pop();
		return true;
	}

	std::shared_ptr<T> try_pop()
	{
		std::lock_guard<std::mutex> lock(mutex);

		if (queue.empty())
			return nullptr;

		std::shared_ptr<T> pointer = std::move(queue.front());
		queue.pop();
		return pointer;
	}

	void wait_and_pop(T &dest)
	{
		std::unique_lock<T> lock(mutex);
		if (queue.empty())
			condv.wait(lock, [this]() { return !queue.empty(); });

		dest = std::move(*queue.front());
		queue.pop();
	}

	std::shared_ptr<T> wait_and_pop()
	{
		std::unique_lock<T> lock(mutex);
		if (queue.empty())
			condv.wait(lock, [this]() { return !queue.empty(); });

		std::shared_ptr<T> pointer = std::move(queue.front());
		queue.pop();
		return pointer;
	}

	bool empty() const
	{
		std::lock_guard<std::mutex> lock(mutex);
		return queue.empty();
	}
};

template <typename T>
class stealing_queue final
{
private:
	std::deque<std::shared_ptr<T>> deque;
	mutable std::mutex mutex;
	std::condition_variable condv;
public:
	stealing_queue() = default;
	stealing_queue(const stealing_queue &) = delete;
	stealing_queue &operator=(const stealing_queue &) = delete;

	void push(T &&element)
	{
		std::shared_ptr<T> ptr = std::make_shared<T>(std::move(element));

		std::lock_guard<std::mutex> lock(mutex);
		deque.push_front(std::move(ptr));
		condv.notify_one();
	}

	bool try_pop(T &dest)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (deque.empty())
			return false;

		dest = std::move(*deque.front());
		deque.pop_front();
		return true;
	}
	std::shared_ptr<T> try_pop()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (deque.empty())
			return nullptr;

		std::shared_ptr<T> ptr{ std::move(deque.front()) };
		deque.pop_front();
		return ptr;
	}
	void wait_and_pop(T &dest)
	{
		std::unique_lock<std::mutex> lock(mutex);
		if (deque.empty())
			condv.wait(lock, [this]() { return !deque.empty(); });

		dest = std::move(*deque.front());
		deque.pop_front();
	}
	std::shared_ptr<T> wait_and_pop()
	{
		std::unique_lock<std::mutex> lock(mutex);
		if (deque.empty())
			condv.wait(lock, [this]() { return !deque.empty(); });

		std::shared_ptr<T> ptr = std::move(*deque.front());
		deque.pop_front();
		return ptr;
	}

	bool try_steal(T &dest)
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (deque.empty())
			return false;

		dest = std::move(*deque.back());
		deque.pop_back();
		return true;
	}
	std::shared_ptr<T> try_steal()
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (deque.empty())
			return nullptr;

		std::shared_ptr<T> ptr = std::move(deque.back());
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
	class moveable_task final
	{
	private:
		struct base_impl
		{
			virtual ~base_impl(){}
			virtual void call() = 0;
		};

		std::unique_ptr<base_impl> implementation;

		template <typename Function>
		struct curr_impl: public base_impl
		{
			Function function;
			curr_impl(Function f) : function{ std::move(f) }
			{}
			void call() override
			{
				function();
			}
		};
	public:
		moveable_task() = default;
		template <typename Function>
		moveable_task(Function function) : implementation{ new curr_impl<Function>(std::move(function)) }
		{}
		moveable_task(const moveable_task &) = delete;
		moveable_task &operator=(const moveable_task &) = delete;
		moveable_task(moveable_task &&other) : implementation{ std::move(other.implementation) }
		{}
		moveable_task &operator=(moveable_task &&other)
		{
			if (&other != this)
				implementation = std::move(other.implementation);
			return *this;
		}

		void operator()()
		{
			if (implementation)
				implementation->call();
		}
	};

	const bool inplace_execution = true;

	std::atomic<bool> terminate_flag;
	mt_safe_queue<moveable_task> common_tasks_queue;
//	std::vector<std::unique_ptr<stealing_queue<moveable_task>>> task_queues;
//	static thread_local stealing_queue<moveable_task> *local_tasks_queue;
//	static thread_local size_t thread_index;

	std::vector<std::thread> threads;
	thread_joiner joiner_of_pool_threads;

/*	bool try_steal(moveable_task &dest)
	{
		for (size_t i = 0; i != task_queues.size(); ++i)
		{
			size_t index = (thread_index + i + 1) % task_queues.size();

			if (task_queues[index] && task_queues[index]->try_steal(dest))
				return true;
		}

		return false;
	}
*/
	void working_loop(size_t index)
	{
//		thread_index = index;
//		local_tasks_queue = task_queues[thread_index].get();
		std::cout << "#" << std::this_thread::get_id() << " got index " << index << std::endl;

		if (inplace_execution)
			return;

		while (!terminate_flag.load())
		{
			moveable_task task;

			if (
			//	(local_tasks_queue && local_tasks_queue->try_pop(task)) ||
				common_tasks_queue.try_pop(task)
			//	|| try_steal(task)
			)
			{
				try
				{
					task();
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
	//		task_queues(std::thread::hardware_concurrency() - 1),
			threads(std::thread::hardware_concurrency() - 1),
			joiner_of_pool_threads{ threads }
	{
		try
		{
		//	for (auto &i: task_queues)
		//		i.reset(nullptr);

		//	for (size_t i = 0; i != threads.size(); ++i)
		//		threads[i] = std::thread(&thread_pool::working_loop, this, i);
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

	template <typename Function, typename Argument>
	void enqueue_task(Function &&function, Argument &&argument)
	{
		if (inplace_execution)
		{
			try
			{
				function(std::move(argument));
			}
			catch (std::exception &e)
			{
				std::cerr << "got an exception: " << e.what() << " in enqueue_task\n";
			}
			catch (...)
			{
				std::cerr << "got an unknown exception in enqueue_task\n";
			}
		}
	/*	else
		{
			moveable_task task{ std::bind(function, std::move(argument)) };

			if (local_tasks_queue)
				local_tasks_queue->push(std::move(task));
			else
				common_tasks_queue.push(std::move(task));
		}
	*/
	}
};

extern std::unique_ptr<thread_pool> worker_threads;

void initialize_thread_pool();

void terminate_thread_pool();

#endif
