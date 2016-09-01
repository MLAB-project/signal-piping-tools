#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void usage(char *prgname)
{
	fprintf(stderr, "%s: usage: %s [-w] [-d] [-p PORT] COMMAND\n", prgname, prgname);
	exit(1);
}

int main(int argc, char *argv[])
{
	short port = 2300;
	int run_in_background = 0;
	int wait_for_child = 0;

	int opt;
	while ((opt = getopt(argc, argv, "dp:w")) != -1) {
		switch (opt) {
			case 'p':
				port = atoi(optarg);
				break;
			case 'd':
				run_in_background = 1;
				break;
			case 'w':
				wait_for_child = 1;
				break;
			default:
				usage(argv[0]);
		}
	}

	if (optind >= argc)
		usage(argv[0]);

	char *cmd = argv[optind];

	int sock;

	sock = socket(AF_INET, SOCK_STREAM, 0);


	if (sock < 0) {
		fprintf(stderr, "%s: socket: %s\n", argv[0], strerror(errno));
		exit(1);
	}

	int optval = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0){
		fprintf(stderr, "%s: socket options: %s\n", argv[0], strerror(errno));
		exit(1);
	}

	struct sockaddr_in addr;
	bzero((char *) &addr, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		fprintf(stderr, "%s: bind: %s\n", argv[0], strerror(errno));
		exit(1);
	}

	if (listen(sock, 1) < 0) {
		fprintf(stderr, "%s: listen: %s\n", argv[0], strerror(errno));
		exit(1);
	}

	if (run_in_background) {
		if (daemon(1, 1) < 0) {
			fprintf(stderr, "%s: daemon: %s\n", argv[0], strerror(errno));
			exit(1);
		}
	}

	while (1) {
		int size;
		struct sockaddr_in caddr;
		int csock = accept(sock, (struct sockaddr *) &caddr, &size);

		if (csock < 0) {
			fprintf(stderr, "%s: accept: %s\n", argv[0], strerror(errno));
			exit(1);
		}

		int pid = fork();

		if (pid < 0) {
			fprintf(stderr, "%s: fork: %s\n", argv[0], strerror(errno));
			exit(1);
		}

		if (pid == 0) {
			dup2(csock, STDIN_FILENO);
			dup2(csock, STDOUT_FILENO);
			close(csock);
			fflush(stderr);
			char *args[4] = {"/bin/sh", "-c", cmd, NULL};
			execvp("/bin/sh", args);
		} else {
			close(csock);
			if (wait_for_child)
				waitpid(pid, NULL, 0);
		}
	}

	return 0;
}
