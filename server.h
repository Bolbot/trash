#ifndef __SERVER_H__
#define __SERVER_H__

#include <ctime>
#include <cctype>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/syscall.h>
#include <sys/sendfile.h>

#include "utils.h"
#include "multithreading.h"
#include "server_classes.h"

struct addrinfo get_addrinfo_hints() noexcept;

int get_binded_socket(struct addrinfo *address_info) noexcept;

int get_listening_socket() noexcept;

void run_server_loop(int master_socket);

void process_the_accepted_connection(active_connection client_fd);

void process_client_request(active_connection &client, http_request request);

const char *http_response_phrase(short status) noexcept;

ssize_t send_status_line(active_connection &client, short status);

ssize_t send_headers(active_connection &client, open_file &file);

void send_client_a_file(active_connection &client, open_file &file) noexcept;

#endif
