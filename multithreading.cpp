#include "multithreading.h"

//thread_local stealing_queue<thread_pool::moveable_task> *thread_pool::local_tasks_queue;

//thread_local size_t thread_pool::thread_index;

std::unique_ptr<thread_pool> worker_threads;

void initialize_thread_pool()
{
	worker_threads.reset(new thread_pool);
}

void terminate_thread_pool()
{
	worker_threads.reset(nullptr);
}
