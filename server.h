#ifndef __SERVER_H__
#define __SERVER_H__

#include <iostream>
#include <thread>


struct addrinfo get_addrinfo_hints() noexcept;

int get_binded_socket(struct addrinfo *address_info) noexcept;

int get_listening_socket() noexcept;

void run_server_loop(int master_socket);

int process_accepted_connection(int socket);
void server(const char *desired_ip, const char *desired_port);

#endif
