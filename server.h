#ifndef __SERVER_H__
#define __SERVER_H__

int process_accepted_connection(int socket);
void server(char *desired_ip, char *desired_port);

#endif
