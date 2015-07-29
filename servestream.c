#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#define MAX_NCONNS 16

void usage(char *prgname)
{
	fprintf(stderr, "%s: usage: %s [-p PORT]\n", prgname, prgname);
	exit(1);
}

int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);

	short port = 2300;

	int opt;
	while ((opt = getopt(argc, argv, "p:")) != -1) {
		switch (opt) {
			case 'p':
				port = atoi(optarg);
				break;
			default:
				usage(argv[0]);
		}
	}

	if (optind < argc)
		usage(argv[0]);

	int sock;

	sock = socket(AF_INET, SOCK_STREAM, 0);

	if (sock < 0) {
		fprintf(stderr, "%s: socket: %s\n", argv[0], strerror(errno));
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

	int wsocks[MAX_NCONNS];
	int i;
	for (i = 0; i < MAX_NCONNS; i++)
		wsocks[i] = -1;

	while (1) {
		int size;
		fd_set rset;

		FD_ZERO(&rset);
		FD_SET(STDIN_FILENO, &rset);
		FD_SET(sock, &rset);

		int rv = select(sock + 1, &rset, NULL, NULL, NULL);

		if (rv < 0) {
			fprintf(stderr, "%s: select: %s\n", argv[0], strerror(errno));
			exit(1);
		}

		if (FD_ISSET(sock, &rset)) {
			struct sockaddr_in caddr;
			int csock = accept(sock, (struct sockaddr *) &caddr, &size);

			if (csock < 0) {
				fprintf(stderr, "%s: accept: %s\n", argv[0], strerror(errno));
				exit(1);
			}

			for (i = 0; i < MAX_NCONNS; i++) {
				if (wsocks[i] < 0) {
					wsocks[i] = csock;
					break;
				}
			}

			if (i >= MAX_NCONNS) {
				fprintf(stderr, "%s: no free slot for the new incoming connection, closing\n");
				close(csock);
			}
		}

		if (FD_ISSET(STDIN_FILENO, &rset)) {
			char buff[8192];

			int ret = read(STDIN_FILENO, buff, sizeof(buff));

			if (ret == 0)
				exit(0);

			if (ret < 0) { /* TODO */
				perror("read");
				exit(1);
			}

			for (i = 0; i < MAX_NCONNS; i++) {
				if (wsocks[i] < 0)
					continue;

				int wret = write(wsocks[i], buff, ret);

				if (wret < 0) {
					close(wsocks[i]);
					wsocks[i] = -1;
					continue;
				}

				if (wret != ret) { /* TODO */
					perror("short write");
					exit(1);
				}
			}
		}
	}

	return 0;
}
