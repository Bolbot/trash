#ifndef __SERVER_H__
#define __SERVER_H__

#include <iostream>
#include <thread>

int process_accepted_connection(int socket);
void server(const char *desired_ip, const char *desired_port);

#endif
