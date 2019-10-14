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

	int master_socket = get_listening_socket();

	run_server_loop(master_socket);

	return 0;
}
