#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

int udp_socket(unsigned short port)
{
	int s = socket(AF_INET, SOCK_DGRAM, 0);

	if (s < 0) {
		perror("socket");
		return -1;
	}

	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = INADDR_ANY;

	if (bind(s, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
		perror("bind");
		close(s);
		return -1;
	}

	return s;
}

void recv_stream(int sock)
{
	int ret;
	uint8_t buffer[1500];

	uint32_t ok = 0;
	uint64_t exp_seq = 0;
	uint64_t exp_offset = 0;

	while (1) {
		if ((ret = recv(sock, &buffer, sizeof(buffer), 0)) < 0) {
			perror("recv");
			continue;
		}

		if (ret < 12) {
			fprintf(stderr, "packet too short\n");
			continue;
		}

		/* hpsdr doesn't seem to care about byte-order, so neither do we */
		uint64_t seq = *((uint64_t *) buffer);
		uint16_t offset = *((uint16_t *) (buffer + 8));
		uint16_t length = *((uint16_t *) (buffer + 10));

		if (length + 12 > ret) {
			fprintf(stderr, "packet length not valid\n");
			continue;
		}

		if (exp_seq == 0) { exp_seq = seq; exp_offset = offset; }

		if ((seq == exp_seq && offset == exp_offset)
			|| (seq == exp_seq + 1 && offset == 0)) {
			ok = 1;
		} else {
			if (ok)
				fprintf(stderr, "packets received out of order (seq: %llu %llu, offset: %hu %hu)\n",
						seq, exp_seq, offset, exp_offset);
			ok = 0;
		}

		exp_seq = seq;
		exp_offset = offset + length;

		if (fwrite(buffer + 12, sizeof(char), length, stdout) != length)
			perror("fwrite");
	}
}

void usage(char *name)
{
	fprintf(stderr, "%s: usage: %s [-p port_to_listen_on]\n", name, name);
}

void sighandler(int signo)
{
	exit(0);
}

int main(int argc, char *argv[])
{
	char c;
	unsigned short stream_port = 2000;
	int sock_stream;

	signal(SIGPIPE, sighandler);
	signal(SIGTERM, sighandler);

	while ((c = getopt(argc, argv, "p:")) != -1) {
		switch (c) {
		case 'p':
			if ((stream_port = atoi(optarg)) == 0) {
				fprintf(stderr, "%s: %s: invalid port number\n", argv[0], optarg);
				return 1;
			}
			break;
		case '?':
			if (optopt == 'p')
				fprintf(stderr, "%s: option -%c requires an argument\n", argv[0], optopt);
			else
				fprintf(stderr, "%s: unknown option '-%c'\n", argv[0], optopt);
			usage(argv[0]);
			return 1;
		}
	}

	if (argc - optind != 0) {
		usage(argv[0]);
		return 1;
	}

	sock_stream = udp_socket(stream_port);
	if (sock_stream < 0)
		return 1;

	recv_stream(sock_stream);

	return 0;
}
