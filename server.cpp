#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <time.h>
#include <netdb.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/syscall.h>
#include <sys/sendfile.h>

#include "response.h"
#include "defines.h"
#include "server.h"
#include "utils.h"

short clients[MAXCLIENTS];

struct addrinfo get_addrinfo_hints() noexcept
{
	struct addrinfo hints;

	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_addrlen = 0;
	hints.ai_addr = nullptr;
	hints.ai_canonname = nullptr;
	hints.ai_next = nullptr;

	return hints;
}

int get_binded_socket(struct addrinfo *address_info) noexcept
{
	int socket_fd = -1;
	bool bind_success = false;

	for (auto it = address_info; it; it = it->ai_next)
	{
		socket_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
		if (socket_fd == -1)
		{
			continue;
		}

		int yes = 1;
		if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
		{
			std::lock_guard<std::mutex> lock(cerr_mutex);
			LOG_CERROR("Program terminates due to setsockopt fail");
			exit(EXIT_FAILURE);
		}

		if (bind(socket_fd, it->ai_addr, it->ai_addrlen) == -1)
		{
			close(socket_fd);
			continue;
		}

		bind_success = true;
		break;
	}

	if (bind_success)
		return socket_fd;
	else
		return -1;
}

int get_listening_socket() noexcept
{
	struct addrinfo hints = get_addrinfo_hints();
	struct addrinfo *address_info;

	int gai_res = getaddrinfo(server_ip.data(), server_port.data(), &hints, &address_info);
	if (gai_res != 0)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		std::cerr << "Error of getaddrinfo: " << gai_strerror(gai_res) << "\n";
		exit(EXIT_FAILURE);
	}

	int socket_fd = get_binded_socket(address_info);

	freeaddrinfo(address_info);

	if (socket_fd == -1)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		std::cerr << "Failed to bind\n";
		exit(EXIT_FAILURE);
	}

	if (listen(socket_fd, SOMAXCONN) == -1)
	{
		std::lock_guard<std::mutex> lock(cerr_mutex);
		LOG_CERROR("Program terminates due to listen error");
		exit(EXIT_FAILURE);
	}

	std::clog << "Listening master socket fd is " << socket_fd << std::endl;

	return socket_fd;
}

void send_error_response(int status, int socket)
{
	if(status < 0) return;
	int sent = send_response(status, 1, "text/html", socket);
	if(sent == -1) { std::cerr << "Failed to send response to " << socket << "\n"; }
	if(VERBOSE) std::cerr << "Successfully sent " << status << " status response to " << socket << " socket\n";
}

int process_accepted_connection(int socket)
{
	char buffer[BUFSIZ] = {0};
	ssize_t recieved = recv(socket, buffer, BUFSIZ, MSG_NOSIGNAL);
	if(recieved < 0) { std::cerr << "Error of recv(): " << strerror(errno); exit(EXIT_FAILURE); }
	std::cerr << "\t[Recieved " << recieved << " bytes from " << socket << " socket]\n";
	if(VERBOSE) std::cerr << "Request is following:\n" << buffer << "\n";

	char address[PATHSIZE] = {0};
	short status = parse_request(buffer, address, PATHSIZE);
	if(VERBOSE) std::cerr << "Status " << status << "\n";
	
	if(abs(status) == 200)
	{
		if(VERBOSE) std::cerr << "Status 200, requested addres is \'" << address << "\'\n";
		size_t file_size = 0;		char content_type[MIMELENGTH] = {0};
		int requested_fd = prepare_file_to_send(address, &file_size, content_type);

		if(requested_fd == -1) { if(status > 0) send_error_response(404, socket); }
		else
		{
			int sent_head = (status > 0) ? send_response(status, file_size, content_type, socket) : 0;
			ssize_t sent_body = sendfile(socket, requested_fd, NULL, file_size);
			if(sent_head == -1 || sent_body == -1) std::cerr << "Some error sending successful response to " << socket << "\n";
			if(VERBOSE) std::cerr << "Sent " << sent_body << "/" << file_size << " of entity body to " << socket << "\n";
		
			if(close(requested_fd)) std::cerr << "Fail of closing " << requested_fd << " descriptor\n";
		}
	}
	else send_error_response(status, socket);

	if(close(socket)) { std::cerr << "Error of closing socket " << socket << "\n"; }
	return 0;
}

void *process_client(void *fd)
{
	if(VERBOSE) std::cerr << "\t\tProcess [" << getpid() << "] Thread [" << std::this_thread::get_id() << "] processing socket #" << *(short*)fd << "\n";
	process_accepted_connection(*(short*)fd);
	*(short*)fd = 0;
	return fd;
}

void run_server_loop(int master_socket)
{
	size_t limit_of_file_descriptors = set_maximal_avaliable_limit_of_fd();
	std::clog << "Processing at most " << limit_of_file_descriptors << " fd at a time." << std::endl;

	while (true)
	{
		int client = accept4(master_socket, NULL, 0, 0 /*SOCK_NONBLOCK*/);
		if(client == -1) { std::cerr << "accept failed: " << strerror(errno) << "\n"; continue; }
		if(VERBOSE) std::cerr << "\t[Accepted socket with descriptor " << client << "]\n";

		short *stored_fd = NULL;
		for(size_t i = 0; i != MAXCLIENTS; ++i) if(!clients[i]) { stored_fd = &clients[i]; break; }
		if(!stored_fd) { std::cerr << "Nowhere to keep connected socket " << client << ", refuse it.\n"; close(client); continue; }
		else *stored_fd = client;

		std::thread thread(process_client, stored_fd);
		if (thread.joinable())
			thread.detach();
	}
}
