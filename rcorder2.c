#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/hash.h>

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
} slot;

typedef struct hasht {
	size_t size;
	slot **slots;
} hasht;

#define die() do {						\
	fprintf(stderr, "ERROR: %s %d\n", __FILE__, __LINE__);	\
	exit(EXIT_FAILURE);					\
} while(0)

#define PUSH(ITEM, HEAD) do {		\
	ITEM->next = HEAD;		\
	if(HEAD)			\
		HEAD->prev = ITEM;	\
	HEAD = ITEM;			\
} while(0)

#define FREE(ITEM, HEAD) do {			\
	typeof (HEAD) tmp;			\
	tmp = ITEM->next;			\
	if(ITEM->next)				\
		ITEM->next->prev = ITEM->prev;	\
	if(ITEM->prev)				\
		ITEM->prev->next = ITEM->next;	\
	else					\
		HEAD = tmp;			\
	free(ITEM);				\
	ITEM = tmp;				\
} while(0)

#define FOREACH(ITEM, HEAD)				\
	for(ITEM = HEAD; ITEM; ITEM = ITEM->next)

void list_alloc_and_push(list **head, void *item);
hasht *hasht_new(size_t size);
void hasht_insert(hasht *h, void *dat, const uint8_t *key, size_t len);
list *hasht_search(hasht *h, const uint8_t *key, size_t len);
script *parse_scripts(int argc, char *argv[]);
script *del_skipped_scripts(script *head, list *skips);
void del_required(hasht *requires, hasht *required, script *cur);
bool has_reqs(hasht *requires, script *cur);

int main(int argc, char *argv[])
{
	list *skips;
	hasht *provided;
	hasht *required;
	hasht *requires;
	script *scripts;

	/* tmps */
	int i;
	list *key;
	list *key2;
	list *lst;
	script *s;
	script *cur;
	script *tmp;

	skips = NULL;

	while ((i = getopt(argc, argv, "s:")) != -1) {
		switch (i) {
		case 's':
			list_alloc_and_push(&skips, optarg);
			break;
		}
	}

	argc -= optind;
	argv += optind;

	scripts = parse_scripts(argc, argv);
	scripts = del_skipped_scripts(scripts, skips);
	provided = hasht_new(1000);
	required = hasht_new(1000);
	requires = hasht_new(1000);

	/* Prepare table: provided */
	FOREACH(cur, scripts) {
		FOREACH(key, cur->provide)
			hasht_insert(provided, cur, key->item,
				     strlen(key->item));
	}

	/* Convert BEFORE to REQUIRE */
	FOREACH(cur, scripts) {
		FOREACH(key, cur->before) {
			lst = hasht_search(provided, key->item,
					   strlen(key->item));
			for(; lst; lst = lst->next) {
				s = lst->item;
				list_alloc_and_push(&s->require, cur->provide->item);
			}
		}
	}

	/* Prepare table: required */
	FOREACH(cur, scripts) {
		FOREACH(key, cur->require)
			hasht_insert(required, cur, key->item, strlen(key->item));
	}

	/* Prepare table: requires */
	FOREACH(cur, scripts) {
		FOREACH(key, cur->require) {
			lst = hasht_search(provided, key->item, strlen(key->item));
			for(; lst; lst = lst->next) {
				tmp = lst->item;
				FOREACH(key2, cur->provide) {
					hasht_insert(requires, tmp, key2->item, strlen(key2->item));
				}
			}
		}
	}

	while(scripts) {
		cur = scripts;
		while(cur) {
			if(!has_reqs(requires, cur)) {
				printf("%s\n", cur->path);
				del_required(requires, required, cur);
				FREE(cur, scripts);
				continue;
			}
			cur = cur->next;
		}
		printf("END\n");
	}

	return EXIT_SUCCESS;
}

void list_alloc_and_push(list **head, void *item)
{
	list *tmp;
	tmp = calloc(1, sizeof(list));
	tmp->item = item;
	PUSH(tmp, (*head));
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

	if(size > 1) {
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
	if(!file) {
		fprintf(stderr, "Cannot open file %s\n", cur->path);
		die();
	}
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
	script *s, *scripts;
	const char *path;
	int i;

	scripts = NULL;
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
		s = parse_lines(path);
		PUSH(s, scripts);
	}
	return scripts;
}

bool must_skip(script *s, list *skips)
{
	list *key;
	list *skip;

	FOREACH(key, s->keyword)
		FOREACH(skip, skips)
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
			FREE(cur, head);
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

size_t hasht_index(hasht *h, const uint8_t *key)
{
	uint32_t hash;
	hash = hash32_str(key, 0);
	return hash % h->size;
}

slot *_hasht_search(hasht *h, const uint8_t *key, size_t len)
{
	size_t index, i;
	slot *slot;
	bool found;

	index = hasht_index(h, key);

	FOREACH(slot, h->slots[index]) {
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
		slot = calloc(1, sizeof(*slot));
		slot->key = key;
		slot->len = len;

		index = hasht_index(h, key);
		PUSH(slot, h->slots[index]);
	}
	list_alloc_and_push(&slot->items, dat);
}

void del_required(hasht *requires, hasht *required, script *cur)
{
	script *scr;
	script *scr2;
	list *prov;
	list *prov2;
	list *lst;
	list *lst2;
	slot *s;

	FOREACH(prov, cur->provide) {
		lst = hasht_search(required, prov->item, strlen(prov->item));
		for(; lst; lst = lst->next) {
			scr = lst->item;

			FOREACH(prov2, scr->provide) {
				s = _hasht_search(requires, prov2->item, strlen(prov2->item));
				lst2 = s->items;
				while(lst2) {
					scr2 = lst2->item;
					if(scr2 == cur) {
						FREE(lst2, s->items);
						continue;
					}
					lst2 = lst2->next;
				}
			}
		}
	}
}

bool has_reqs(hasht *requires, script *cur)
{
	list *lst;
	list *key;

	FOREACH(key, cur->provide) {
		lst = hasht_search(requires, key->item, strlen(key->item));
		if(lst && lst->item)
			return true;
	}
	return false;
}
