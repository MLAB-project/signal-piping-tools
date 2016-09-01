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
	fprintf(stderr, "%s: usage: %s [-d] [-p PORT]\n", prgname, prgname);
	exit(1);
}

ssize_t writen(int fd, const void *buf, size_t count)
{
	size_t written;
	ssize_t ret;

	written = 0;
	while (written < count) {
		ret = write(fd, ((char *) buf) + written, count - written);

		if (ret <= 0) {
			if (ret < 0 && errno == EINTR)
				continue;

			return ret;
		}

		written += ret;
	}

	return written;
}

int main(int argc, char *argv[])
{
	signal(SIGPIPE, SIG_IGN);

	short port = 2300;
	int run_in_background = 0;

	int opt;
	while ((opt = getopt(argc, argv, "dp:")) != -1) {
		switch (opt) {
			case 'p':
				port = atoi(optarg);
				break;
			case 'd':
				run_in_background = 1;
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
				fprintf(stderr, "%s: no free slot for "
								"incoming connection, closing\n", argv[0]);
				close(csock);
			}
		}

		if (FD_ISSET(STDIN_FILENO, &rset)) {
			char buff[8192];

			int ret = read(STDIN_FILENO, buff, sizeof(buff));

			if (ret == 0)
				exit(0);

			if (ret < 0) {
				fprintf(stderr, "%s: read: %s\n", argv[0], strerror(errno));
				exit(1);
			}

			for (i = 0; i < MAX_NCONNS; i++) {
				if (wsocks[i] < 0)
					continue;

				int wret = writen(wsocks[i], buff, ret);

				if (wret != ret) {
					fprintf(stderr, "%s: write error, "
									"closing connection\n", argv[0]);
					close(wsocks[i]);
					wsocks[i] = -1;
				}
			}
		}
	}

	return 0;
}
