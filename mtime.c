#include <stdio.h>
#include <sys/time.h>

int main()
{
	struct timeval t1;
	double time;
	gettimeofday(&t1, NULL);
	time = t1.tv_sec + (t1.tv_usec / 1000000.0);
	printf("%f", time);
	return 0;
}
