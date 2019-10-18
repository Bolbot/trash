#include "multithreading.h"

namespace actual
{
	thread_local stealing_queue<thread_pool::moveable_task> *thread_pool::local_tasks_queue;

	thread_local size_t thread_pool::thread_index;
}

#include "server_classes.h"
namespace concrete
{
	thread_local stealing_queue *thread_pool::local_tasks_queue;

	thread_local size_t thread_pool::thread_index;
}
