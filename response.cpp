#include <time.h>
#include <regex.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "response.h"
#include "defines.h"

extern FILE *_myoutput;

char *readline_CRLF(char *str)
{
	size_t len = strlen(str);
	if(!len || !strstr(str, "\n")) return NULL;
	char *end = strstr(str, "\r\n");
	if(!end) end = strstr(str, "\n");

	ptrdiff_t line_len = end - str;
	char *line = (char*)malloc((line_len + 1) * sizeof(char));
	memset(line, 0, line_len + 1);
	strncpy(line, str, line_len);

	char *next = end + ((*end == '\r') ? 2 : 1);
	char *start = str;
	while((*start++ = *next++));
	return line;
}

short parse_request(char *init_buffer, char *addr, size_t addr_size)
{
	short http09 = 0;
	short status = 0;

	ssize_t total = strlen(init_buffer);
	if(total <= 0) fprintf(_myoutput, "Fail, buffer of request is empty\n");
	if(!strstr(init_buffer, "\n")) { fprintf(_myoutput,"Found no \\n in %s\n", init_buffer); return 400; }

	char buffer[BUFSIZ] = {0};	strncpy(buffer, init_buffer, BUFSIZ);

	char *request_line = readline_CRLF(buffer);
	if(VERBOSE) fprintf(_myoutput, "Identifying request-line: '%s' (length %lu)\n", request_line, strlen(request_line));
	if((strlen(request_line) < 5) || !strstr(request_line, " ")) return 400;

	regex_t reg;
	const char regex_request_line[] = "^((POST)|(GET)|(HEAD))[ ][^ ]+([ ]((HTTP/)[0-9].[0-9]))?$";
	if(regcomp(&reg, regex_request_line, REG_EXTENDED)) fprintf(_myoutput, "Failed to set regexp (request)\n");
	if(regexec(&reg, request_line, 0, NULL, 0) == REG_NOMATCH) status = 400;

	char *httpver = strstr(request_line, "HTTP/");
	if(!httpver) http09 = 1;
	else if(!strncmp(httpver, "HTTP/0.9", 8)) http09 = 1;
	else if(strncmp(httpver, "HTTP/1.", 7)) return 505;
	if(http09 && strncmp(request_line, "GET ", 4)) { fprintf(_myoutput, "HTTP/0.9 uses not GET, 400 at once\n"); return -400; }

	memset(addr, 0, addr_size);	sscanf(request_line, "%*s %s", addr);
	if(VERBOSE) fprintf(_myoutput, "\t\tReturned URI as %s\n", addr);

	if(!strncmp(request_line, "POST ", 5) || !strncmp(request_line, "HEAD ", 5)) status = ((status) ? status : 405);
	if(!status) status = 200;

	free(request_line);
	if(http09) { fprintf(_myoutput, "This is HTTP/0.9, returning status %d right away.\n", status); return -status; }

	char *header = NULL;

	while(header = readline_CRLF(buffer))
	{
		regex_t reg_header;
		const char regex_field_header[] = "^[^][[:cntrl:]()<>@,:;\"/?={} 	]+:[^[:cntrl:]]*$";
		if(regcomp(&reg_header, regex_field_header, REG_EXTENDED)) fprintf(_myoutput, "Failed to set regexp (headers)\n");

		if(strlen(header) < 2 && header[0]) fprintf(_myoutput, "\t\tRequest END letter '%c' (%d)\n", *header, *header);
		else if(strlen(header) > 2 && regexec(&reg_header, header, 0, NULL, 0) == REG_NOMATCH)
			fprintf(_myoutput, "%s - not a vaild header!\n", header);
		free(header);
	}
	return status;
}

int send_response(short status, size_t content_length, const char *content_type, int dest_socket_fd)
{
	time_t timet = time(NULL);	char date[DATELENGTH] = {0};
	sprintf(date, "%s", asctime(localtime(&timet)));	
	if(date[strlen(date) - 1] == '\n') date[strlen(date) - 1] = '\0';
	if(VERBOSE) fprintf(_myoutput, "Sending status %d response at '%s'\n", status, date);

	char http_version1[] = "HTTP/1.0";			char *response = NULL;
	char response200[] = "OK";				char response400[] = "Bad Request";
	char response404[] = "Not Found";			char response405[] = "Method Not Allowed";
	char response505[] = "HTTP Version not supported";	char responseERR[] = "Unknown Yet Status Code";
	switch(status)
	{
	case 200: response = response200; break;
	case 400: response = response400; break;
	case 404: response = response404; break;
	case 405: response = response405; break;
	case 505: response = response505; break;
	default: response = responseERR; fprintf(_myoutput, "FAILED to find definition to status %d\n", status);
	};

	char buffer[BUFSIZ] = {0};
	sprintf(buffer, "%s %d %s\r\nDate: %s\r\nContent-Length: %lu\r\nContent-Type: %s\r\n\r\n",
			http_version1, status, response, date, content_length, content_type);

	int writeres = send(dest_socket_fd, buffer, strlen(buffer), MSG_NOSIGNAL);
	if(writeres < 0) { fprintf(_myoutput, "Fail of sending %d response: %s\n", status, strerror(errno)); return -1; }
	if(VERBOSE) fprintf(_myoutput, "Sent response status %d (total length was %ld)\n", status, strlen(buffer));

	return 0;
}
