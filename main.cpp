#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "defines.h"
#include "server.h"
#include "utils.h"

int main(int argc, char **argv)
{
	parse_program_options(argc, argv);

	daemonize();

	server(server_ip.data(), server_port.data());

	return 0;
}
