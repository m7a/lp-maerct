struct list {
	size_t size;
	struct list_data* start;
	struct list_data* end;
};

#define FOREACH(L, I) for(I = (L)->start; I != NULL; I = I->next) /* Iterator */

struct list* alloc_list();
void add_to_list(struct list* list, void* data);
void free_list(struct list* list);
void free_list_cnt(struct list* list);
void free_list_ncnt(struct list* list);
int list_empty(struct list* list);
