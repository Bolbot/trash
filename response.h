#ifndef __RESPONSE_H_
#define __RESPONSE_H_

char *readline_CRLF(char *str);
short parse_request(char *init_buffer, char *addr, size_t addr_size);
int send_response(short status, size_t content_length, const char *content_type, int dest_socket_fd);

#endif
