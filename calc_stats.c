#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

double z, e;

typedef struct stats_t {
	int N;
	double average, variance, deviation;
} stats_t;

int read_values(const char *fname, double *values);
void show_stats(stats_t *s, int n, bool show_n);
void calc_stats_t(int first, int last, double *values, stats_t *s);

int main(int argc, char *argv[])
{
	int l, n, i, s;
	double values[1000], avgs[10];
	stats_t stats[50];

	if(argc < 4)
		exit(1);
	z = atof(argv[1]);
	e = atof(argv[2]);
	n = atoi(argv[3]);
	l = read_values(argv[4], values);
	s = l / n;

	for(i = 0; i < s; i++) {
		calc_stats_t(i * n, (i + 1) * n, values, &stats[i]);
		avgs[i] = stats[i].average;
	}
	if(s == 1)
		show_stats(stats, s, true);
	else
		show_stats(stats, s, false);
	if(s > 1) {
		printf("Distribuicao amostral:\n");
		calc_stats_t(0, s, avgs, &stats[s]);
		show_stats(&stats[s], 1, false);
	}
	return 0;
}

int read_values(const char *fname, double *values)
{
	FILE *file;
	int ret, n;
	double v;
	n = 0;
	file = fopen(fname, "r");
	if(!file)
		exit(1);
	while(!feof(file)) {
		ret = fscanf(file, "%lf", &v);
		if(ret != 0 && ret != EOF)
			values[n++] = v;
	}
	fclose(file);
	return n;
}

void calc_stats_t(int first, int last, double *values, stats_t *s)
{
	int i, n;
	double sum, tmp;
	sum = 0;
	n = last - first;
	for(i = first; i < last; i++)
		sum += values[i];
	s->average = sum / n;
	for(i = first; i < last; i++) {
		tmp = values[i] - s->average;
		s->variance += (tmp * tmp);
	}
	s->variance = s->variance/(n - 1);
	s->deviation = sqrt(s->variance);
	s->N = s->variance * (z * z) / (e * e);
}

void show_stats(stats_t *s, int n, bool show_n)
{
	int i;
	printf("%s %6s %13s %15s",
	       "Amostra", "Media", "Variancia", "Desvio Padrao");
	if(show_n)
	  	printf(" %3s", "N");
	printf("\n");
	for(i = 0; i < n; i++, s++) {
		printf("%d %15lf %9lf %11lf",
		       i + 1, s->average, s->variance, s->deviation);
		if(show_n)
			printf(" %9d", s->N);
		printf("\n");
	}
}
