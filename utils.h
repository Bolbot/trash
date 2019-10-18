#ifndef __UTILS_H__
#define __UTILS_H__

#include <boost/program_options.hpp>

#include <sys/time.h>
#include <sys/resource.h>

#include "logging.h"
#include "file_wrapper.h"
#include "multithreading.h"

void parse_program_options(int argc, char **argv) noexcept;

void daemonize() noexcept;

void signal_handler(int signal_number) noexcept;

void set_signal(int signal_number) noexcept;

void set_signals() noexcept;

size_t set_maximal_avaliable_limit_of_fd() noexcept;

void atexit_terminator() noexcept;

[[noreturn]] void terminate_handler() noexcept;

#endif
