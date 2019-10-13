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

void server(const char *desired_ip, const char *desired_port)
{
	/*	1. Prepare and create socket with commands getaddrinfo() and socket() */

	struct addrinfo hints; bzero((void*)&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	struct addrinfo *addr;
	int gai_res = getaddrinfo(desired_ip, desired_port, &hints, &addr);
	if(gai_res) { std::cerr << "Failed to getaddrinfo(): " << gai_strerror(gai_res) << ". It's quit.\n"; exit(EXIT_FAILURE); }

	int socket_descriptor = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
	if(socket_descriptor == -1) { std::cerr << "Failed to get socket(): << " << strerror(errno) << "\n"; exit(EXIT_FAILURE); }
	if(VERBOSE) std::cerr << "\t[Got master socket_descriptor " << socket_descriptor << "]\n";

	/*	2. Prepare and bind socket with commands setsockopt() and bind() */

	int yes = 1;
	if(setsockopt(socket_descriptor, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
		std::cerr << "Failed to setsockopt() to SO_REUSEADDR: " << strerror(errno) << "\n";

	if(bind(socket_descriptor, addr->ai_addr, addr->ai_addrlen) == -1)
	{
		std::cerr << "Failed to bind() on socket " << socket_descriptor << ": " << strerror(errno) << "\n";
		exit(EXIT_FAILURE);
	}

	/*	3. Start listening on socket with command listen()	*/
	if(listen(socket_descriptor, SOMAXCONN) == -1)
		std::cerr << "Failed to set master socket listening: " << strerror(errno) << "\n";

	while("true")
	{
	/*	4. Accepting incoming connections to nonblocking sockets with command accept4()	*/
		int client = accept4(socket_descriptor, NULL, 0, 0 /*SOCK_NONBLOCK*/);
		if(client == -1) { std::cerr << "accept failed: " << strerror(errno) << "\n"; continue; }
		if(VERBOSE) std::cerr << "\t[Accepted socket with descriptor " << client << "]\n";

	/*	5. Processing HTTP stuff, then closing sockets with command close()	*/

		short *stored_fd = NULL;
		for(size_t i = 0; i != MAXCLIENTS; ++i) if(!clients[i]) { stored_fd = &clients[i]; break; }
		if(!stored_fd) { std::cerr << "Nowhere to keep connected socket " << client << ", refuse it.\n"; close(client); continue; }
		else *stored_fd = client;

		constexpr bool USE_PTHREAD = false;

		if (USE_PTHREAD)
		{
			pthread_t thread;	int perr;	//	void *thread_ret;
			if((perr = pthread_create(&thread, NULL, process_client, stored_fd)))
				std::cerr << "pthread_create fail: " << strerror(perr) << "\n";
			else if((perr = pthread_detach(thread)))
				std::cerr << "pthread_detach fail: " << strerror(perr) << "\n";
		}
		else
		{
			std::thread thread(process_client, stored_fd);
			if (thread.joinable())
				thread.detach();
		}
	}
	shutdown(socket_descriptor, SHUT_RDWR);
}
