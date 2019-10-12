#define _GNU_SOURCE

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

extern FILE *_myoutput;

short clients[MAXCLIENTS];

void send_error_response(int status, int socket)
{
	if(status < 0) return;
	int sent = send_response(status, 1, "text/html", socket);
	if(sent == -1) { fprintf(_myoutput, "Failed to send response to %d\n", socket); }
	if(VERBOSE) fprintf(_myoutput, "Successfully sent %d status response to %d socket\n", status, socket);
}

int process_accepted_connection(int socket)
{
	char buffer[BUFSIZ] = {0};
	ssize_t recieved = recv(socket, buffer, BUFSIZ, MSG_NOSIGNAL);
	if(recieved < 0) { fprintf(_myoutput, "Error of recv(): %s", strerror(errno)); exit(EXIT_FAILURE); }
	fprintf(_myoutput, "\t[Recieved %ld bytes from %d socket]\n", recieved, socket);
	if(VERBOSE) fprintf(_myoutput, "Request is following:\n%s\n", buffer);

	char address[PATHSIZE] = {0};
	short status = parse_request(buffer, address, PATHSIZE);
	if(VERBOSE) fprintf(_myoutput, "Status %d\n", status);
	
	if(abs(status) == 200)
	{
		if(VERBOSE) fprintf(_myoutput, "Status 200, requested addres is '%s'\n", address);
		size_t file_size = 0;		char content_type[MIMELENGTH] = {0};
		int requested_fd = prepare_file_to_send(address, &file_size, content_type);

		if(requested_fd == -1) { if(status > 0) send_error_response(404, socket); }
		else
		{
			if(VERBOSE) fprintf(_myoutput, "Preparing to send %d a %s%lu bytes file\n",
					socket, ((status > 0) ? "status line and a " : ""), file_size);
			int sent_head = (status > 0) ? send_response(status, file_size, content_type, socket) : 0;
			ssize_t sent_body = sendfile(socket, requested_fd, NULL, file_size);
			if(sent_head == -1 || sent_body == -1) fprintf(_myoutput, "Some error sending successful response to %d\n", socket);
			if(VERBOSE) fprintf(_myoutput, "Sent %ld/%lu of entity body to %d\n", sent_body, file_size, socket);
		
			if(close(requested_fd)) fprintf(_myoutput, "Fail of closing %d descriptor\n", requested_fd);
		}
	}
	else send_error_response(status, socket);

	if(close(socket)) { fprintf(_myoutput, "Error of closing socket %d\n", socket); }
	return 0;
}

void *process_client(void *fd)
{
	if(VERBOSE) fprintf(_myoutput, "\t\tProcess [%d] Thread [%ld] processing socket #%d\n", getpid(), syscall(SYS_gettid), *(short*)fd);
	process_accepted_connection(*(short*)fd);
	*(short*)fd = 0;
	return fd;
}

void server(char *desired_ip, char *desired_port)
{
	/*	1. Prepare and create socket with commands getaddrinfo() and socket() */

	struct addrinfo hints; bzero((void*)&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	struct addrinfo *addr;
	int gai_res = getaddrinfo(desired_ip, desired_port, &hints, &addr);
	if(gai_res) { fprintf(_myoutput, "Failed to getaddrinfo(): %s. It's quit.\n", gai_strerror(gai_res)); exit(EXIT_FAILURE); }

	int socket_descriptor = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
	if(socket_descriptor == -1) { fprintf(_myoutput, "Failed to get socket(): %s\n", strerror(errno)); exit(EXIT_FAILURE); }
	if(VERBOSE) fprintf(_myoutput, "\t[Got master socket_descriptor %d]\n", socket_descriptor);

	/*	2. Prepare and bind socket with commands setsockopt() and bind() */

	int yes = 1;
	if(setsockopt(socket_descriptor, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
		fprintf(_myoutput, "Failed to setsockopt() to SO_REUSEADDR: %s\n", strerror(errno));

	if(bind(socket_descriptor, addr->ai_addr, addr->ai_addrlen) == -1)
	{
		fprintf(_myoutput, "Failed to bind() on socket %d: %s\n", socket_descriptor, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/*	3. Start listening on socket with command listen()	*/
	if(listen(socket_descriptor, SOMAXCONN) == -1)
		fprintf(_myoutput, "Failed to set master socket listening: %s\n", strerror(errno));

	while("true")
	{
	/*	4. Accepting incoming connections to nonblocking sockets with command accept4()	*/
		int client = accept4(socket_descriptor, NULL, 0, 0 /*SOCK_NONBLOCK*/);
		if(client == -1) { fprintf(_myoutput, "accept failed: %s\n", strerror(errno)); continue; }
		if(VERBOSE) fprintf(_myoutput, "\t[Accepted socket with descriptor %d]\n", client);

	/*	5. Processing HTTP stuff, then closing sockets with command close()	*/

		short *stored_fd = NULL;
		for(size_t i = 0; i != MAXCLIENTS; ++i) if(!clients[i]) { stored_fd = &clients[i]; break; }
		if(!stored_fd) { fprintf(_myoutput, "Nowhere to keep connected socket %d, refuse it.\n", client); close(client); continue; }
		else *stored_fd = client;

		pthread_t thread;	int perr;		void *thread_ret;
		if((perr = pthread_create(&thread, NULL, process_client, stored_fd)))
			fprintf(_myoutput, "pthread_create fail: %s\n", strerror(perr));
		else if((perr = pthread_detach(thread)))
			fprintf(_myoutput, "pthread_detach fail: %s\n", strerror(perr));
	}
	shutdown(socket_descriptor, SHUT_RDWR);
}
