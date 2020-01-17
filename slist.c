#include <stdlib.h>

#include "slist.h"

struct list* alloc_list()
{
	struct list* list = malloc(sizeof(struct list));
	list->size  = 0;
	list->start = NULL;
	list->end   = NULL;
	return list;
}

void free_list(struct list* list)
{
	free_list_cnt(list);
	free(list);
}

void free_list_ncnt(struct list* list)
{
	struct list_data* i = list->start;
	struct list_data* bak;
	while(i != NULL) {
		bak = i;
		i = i->next;
		free(bak);
	}
	list->size  = 0;
	list->start = NULL;
	list->end   = NULL;
}

void free_list_cnt(struct list* list)
{
	struct list_data* i = list->start;
	struct list_data* bak;
	while(i != NULL) {
		free(i->data);
		bak = i;
		i = i->next;
		free(bak);
	}
	list->size  = 0;
	list->start = NULL;
	list->end   = NULL;
}

int list_empty(struct list* list)
{
	return list->start == NULL;
}

void add_to_list(struct list* list, void* data)
{
	struct list_data* entry = malloc(sizeof(struct list_data));
	entry->next = NULL;
	entry->data = data;
	if(list->end == NULL)
		list->start = entry;
	else
		list->end->next = entry;
	list->end = entry;
	list->size++;
}
