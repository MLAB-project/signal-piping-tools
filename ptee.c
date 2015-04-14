#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char const *argv[])
{
	int i;

	int *outputs = (int *) malloc(sizeof(int) * argc);
	outputs[0] = STDOUT_FILENO;

	for (i = 1; i < argc; i++) {
		int pipefd[2];
		pipe(pipefd);

		int pid = fork();

		if (pid < 0) {
			fprintf(stderr, "%s: fork: %s\n", argv[0], strerror(errno));
			exit(1);
		}

		if (pid == 0) {
			dup2(pipefd[0], STDIN_FILENO);
			char *args[4] = {"/bin/sh", "-c", (char *) argv[i], NULL};
			execvp("/bin/sh", args);
		} else {
			close(pipefd[0]);
			outputs[i] = pipefd[1];
		}
	}

	while (1) {
		char buf[8192];
		ssize_t ret = read(STDIN_FILENO, buf, sizeof(buf));

		if (ret < 0) {
			perror("read");
			exit(1);
		}

		for (i = 0; i < argc; i++) {
			int len = ret;
			char *bufp = buf;

			while (len) {
				int wret = write(outputs[i], bufp, len);

				if (wret < 0) {
					perror("write");
					exit(1);
				}

				len -= wret;
				bufp += wret;
			}
		}
	}

	return 0;
}
