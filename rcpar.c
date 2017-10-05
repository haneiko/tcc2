#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

#define die()								\
	do {								\
		fprintf(stderr, "ERROR: %s %d\n", __FILE__, __LINE__);	\
		exit(EXIT_FAILURE);					\
	} while(0)

int main(int argc, char *argv[])
{
	int i, status;
	pid_t pid;
	const char *start_type;

	start_type = NULL;

	while ((i = getopt(argc, argv, "t:")) != -1) {
		switch (i) {
		case 't':
			start_type = optarg;
			break;
		}
	}

	argc -= optind;
	argv += optind;

	for (i = 0; i < argc; i++) {
		/* printf("%s\n", argv[i]); */
		if ((pid = fork()) == -1)
			die();
		if(pid == 0) {
			execl("/bin/sh", "sh", argv[i], start_type, (char *)NULL);
			die();
		}
	}

	while(wait(&status) > 0);

	return EXIT_SUCCESS;
}
