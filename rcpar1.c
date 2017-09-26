#include <getopt.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct list {
	void *item;
	struct list *next;
	struct list *prev;
} list;

typedef struct script {
	list *provide;
	list *require;
	list *before;
	list *keyword;
	struct script *next;
	struct script *prev;
	bool printed;
	const char *path;
} script;

typedef struct slot {
	const uint8_t *key;
	size_t len;
	list *items;
	struct slot *next;
	struct slot *prev;
	int exec;
	pthread_mutex_t exec_mutex;
	pthread_cond_t exec_cond;
} slot;

typedef struct hasht {
	size_t size;
	slot **slots;
} hasht;

typedef struct thr_args {
	script *scr;
	hasht *provided;
	const char *start_type;
} thr_args;

#define die()	  \
	do { \
		fprintf(stderr, "ERROR: %s %d\n", __FILE__, __LINE__); \
		exit(EXIT_FAILURE); \
	} while(0)

#define LIST_PUSH(HEAD, ITEM)	  \
	do { \
		ITEM->next = HEAD; \
		if(HEAD) \
			HEAD->prev = ITEM; \
		HEAD = ITEM; \
	} while(0)

#define LIST_FREE(HEAD, ITEM)	  \
	do { \
		typeof (HEAD) tmp; \
		tmp = ITEM->next; \
		if(ITEM->next) \
			ITEM->next->prev = ITEM->prev; \
		if(ITEM->prev) \
			ITEM->prev->next = ITEM->next; \
		else \
			HEAD = tmp; \
		free(ITEM); \
		ITEM = tmp; \
	} while(0)

void list_alloc_and_push(list **head, void *item);
hasht *hasht_new(size_t size);
void hasht_insert(hasht *h, void *dat, const uint8_t *key, size_t len);
list *hasht_search(hasht *h, const uint8_t *key, size_t len);
script *parse_scripts(int argc, char *argv[]);
script *del_skipped_scripts(script *head, list *skips);
void start_all_thrs(script *scripts, size_t size, hasht *provided,
                    const char *start_type);

int main(int argc, char *argv[])
{
	list *skips;
	hasht *provided;
	script *head;
	size_t size;
	const char *start_type;

	/* tmps */
	int i;
	list *key;
	list *lst;
	script *s;
	script *cur;

	skips = NULL;
	start_type = NULL;
	size = 0;

	while ((i = getopt(argc, argv, "s:t:")) != -1) {
		switch (i) {
		case 's':
			list_alloc_and_push(&skips, optarg);
			break;
		case 't':
			start_type = optarg;
			break;
		}
	}

	argc -= optind;
	argv += optind;

	head = parse_scripts(argc, argv);
	head = del_skipped_scripts(head, skips);
	provided = hasht_new(1000);

	/* Prepare table: provided */
	for(cur = head; cur; cur = cur->next, size++) {
		for(key = cur->provide; key; key = key->next)
			hasht_insert(provided, cur, key->item, strlen(key->item));
	}

	/* Convert BEFORE to REQUIRE */
	for(cur = head; cur; cur = cur->next) {
		for(key = cur->before; key; key = key->next) {
			lst = hasht_search(provided, key->item, strlen(key->item));
			for(; lst; lst = lst->next) {
				s = lst->item;
				list_alloc_and_push(&s->require, cur->provide->item);
			}
		}
	}

	start_all_thrs(head, size, provided, start_type);

	return EXIT_SUCCESS;
}

void list_alloc_and_push(list **head, void *item)
{
	list *tmp;
	tmp = calloc(1, sizeof(list));
	tmp->item = item;
	LIST_PUSH((*head), tmp);
}

char *getline(FILE *file)
{
	char c, *buffer;
	size_t size;
	fpos_t begin;

	buffer = NULL;
	size = 0;

	if(fgetpos(file, &begin))
		die();

	do {
		c = fgetc(file);
		size++;
	} while(c != '\n' && c != EOF);
	size++;

	if(size > 0) {
		if(fsetpos(file, &begin))
			die();
		buffer = malloc(size * sizeof(char));
		fgets(buffer, size, file);
		buffer[size - 1] = '\0';
	}
	return buffer;
}

list *split(const char *buffer, char delim)
{
	list *strings;
	const char *begin;
	const char *end;
	size_t size;
	char *tmp;

	strings = NULL;
	begin = end = buffer;

	while((end = strchr(end, delim)) != NULL) {
		size = end - begin + 1;

		if(size > 1) {
			tmp = malloc(size * sizeof(char));
			strncpy(tmp, begin, size);
			tmp[size - 1] = '\0';
			list_alloc_and_push(&strings, tmp);
		}
		++end;
		begin = end;
	}

	size = strlen(begin);
	/* size = strlen(begin) + 1; */
	tmp = malloc(size * sizeof(char));
	strncpy(tmp, begin, size);
	tmp[size - 1] = '\0';
	list_alloc_and_push(&strings, tmp);

	return strings;
}

script *parse_lines(const char *path)
{
	bool parsing;
	char *buffer;
	FILE *file;
	script *cur;

	cur = calloc(1, sizeof(script));
	cur->path = path;
	file = fopen(cur->path, "r");
	if(!file)
		die();
	parsing = false;

	while((buffer = getline(file))) {
		if(strstr(buffer, "# PROVIDE: ")) {
			cur->provide = split(buffer + 11, ' ');
			parsing = true;
		} else if(strstr(buffer, "# REQUIRE: ")) {
			cur->require = split(buffer + 11, ' ');
			parsing = true;
		} else if(strstr(buffer, "# BEFORE: ")) {
			cur->before = split(buffer + 10, ' ');
			parsing = true;
		} else if(strstr(buffer, "# KEYWORD: ")) {
			cur->keyword = split(buffer + 11, ' ');
			parsing = true;
		} else if(parsing) {
			break;
		}
	}
	fclose(file);
	return cur;
}

script *parse_scripts(int argc, char *argv[])
{
	script *cur, *head;
	const char *path;
	int i;

	head = NULL;
	for(i = 0; i < argc; i++) {
		path = argv[i];

		if(strstr(path, ".sh")
		   || strstr(path, ".OLD")
		   || strstr(path, ".bak")
		   || strstr(path, ".orig")
		   || strstr(path, ",v")
		   || strstr(path, "#")
		   || strstr(path, "~")) {
			/* printf("ignoring %s\n", path); */
			continue;
		}
		cur = parse_lines(path);
		LIST_PUSH(head, cur);
	}
	return head;
}

bool must_skip(script *s, list *skips)
{
	list *key;
	list *skip;

	for(key = s->keyword; key; key = key->next)
		for(skip = skips; skip; skip = skip->next)
			if(strcmp(key->item, skip->item) == 0)
				return true;
	return false;
}

script *del_skipped_scripts(script *head, list *skips)
{
	script *cur;
	cur = head;

	while(cur) {
		if(must_skip(cur, skips)) {
			/* printf("skipping '%s'\n", cur->path); */
			LIST_FREE(head, cur);
			continue;
		}
		cur = cur->next;
	}
	return head;
}

hasht *hasht_new(size_t size)
{
	hasht *h;
	h = malloc(sizeof(hasht));
	h->size = size;
	h->slots = calloc(size, sizeof(slot *));
	return h;
}

uint32_t jenkins_one_at_a_time_hash(const uint8_t* key, size_t length)
{
	size_t i;
	uint32_t hash;
	i = 0;
	hash = 0;
	while (i != length) {
		hash += key[i++];
		hash += hash << 10;
		hash ^= hash >> 6;
	}
	hash += hash << 3;
	hash ^= hash >> 11;
	hash += hash << 15;
	return hash;
}

size_t hasht_index(hasht *h, const uint8_t *key, size_t len)
{
	uint32_t hash;
	hash = jenkins_one_at_a_time_hash(key, len);
	return hash % h->size;
}

slot *_hasht_search(hasht *h, const uint8_t *key, size_t len)
{
	size_t index, i;
	slot *slot;
	bool found;

	index = hasht_index(h, key, len);
	slot = h->slots[index];

	for(; slot; slot = slot->next) {
		if(len != slot->len)
			continue;

		found = true;
		for(i=0; i<len; i++)
			if(key[i] != slot->key[i])
				found = false;
		if(found)
			break;
	}
	return slot;
}

list *hasht_search(hasht *h, const uint8_t *key, size_t len)
{
	slot *slot;
	slot = _hasht_search(h, key, len);
	if(slot)
		return slot->items;
	return NULL;
}

void hasht_insert(hasht *h, void *dat, const uint8_t *key, size_t len)
{
	slot *slot;
	size_t index;

	slot = _hasht_search(h, key, len);

	if(!slot) {
		index = hasht_index(h, key, len);
		slot = calloc(1, sizeof(*slot));
		slot->key = key;
		slot->len = len;
		pthread_mutex_init(&slot->exec_mutex, NULL);
		pthread_cond_init(&slot->exec_cond, NULL);

		LIST_PUSH(h->slots[index], slot);
	}
	list_alloc_and_push(&slot->items, dat);
}

void exec_process(const char *path, const char *arg)
{
	pid_t pid;
	int status;

	if((pid = fork()) == -1)
		die();
	if(pid == 0) {
		execl("/bin/sh", "sh", path, arg, (char *)NULL);
		die();
	}
	wait(&status);
}

void *thread(void *arg)
{
	script *scr;
	list *lst;
	slot *slo;
	thr_args *targs;
	hasht *provided;

	targs = arg;
	scr = targs->scr;
	provided = targs->provided;
	lst = scr->require;

	while(lst) {
		slo = _hasht_search(provided, lst->item, strlen(lst->item));
		LIST_FREE(scr->require, lst);

		if(slo) {
			pthread_mutex_lock(&slo->exec_mutex);
			while(!slo->exec)
				pthread_cond_wait(&slo->exec_cond, &slo->exec_mutex);
			pthread_mutex_unlock(&slo->exec_mutex);
		}
	}

	/* printf("EXECUTANDO %s\n", scr->path); */
	exec_process(scr->path, targs->start_type);
	lst = scr->provide;

	while(lst) {
		slo = _hasht_search(provided, lst->item, strlen(lst->item));
		LIST_FREE(scr->provide, lst);

		pthread_mutex_lock(&slo->exec_mutex);
		slo->exec = 1;
		pthread_cond_broadcast(&slo->exec_cond);
		pthread_mutex_unlock(&slo->exec_mutex);
	}
	return NULL;
}

void start_all_thrs(script *scripts, size_t size, hasht *provided,
                    const char *start_type)
{
	pthread_t threads[size];
	pthread_attr_t attr;
	size_t i;
	thr_args *targs;

	pthread_attr_init(&attr);
	if(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE))
		die();
	if(pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM))
		die();

	for(i = 0; scripts; scripts = scripts->next, i++) {
		targs = calloc(1, sizeof(thr_args));
		targs->scr = scripts;
		targs->start_type = start_type;
		targs->provided = provided;
		if(pthread_create(&threads[i], &attr, thread, targs))
			die();
	}

	for (i = 0; i < size; i++)
		if(pthread_join(threads[i], NULL))
			die();

	pthread_attr_destroy(&attr);
	pthread_exit(NULL);
}
