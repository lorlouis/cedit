#ifndef LINKEDLIST_H
#define LINKEDLIST_H 1

#include <stddef.h>

#define container_of(ptr, ty, member) (ty*)((char*)(ptr) - offsetof(ty, member))

struct DLinkedList {
    struct DLinkedList *prev;
    struct DLinkedList *next;
};

// will lookup the nth node after start, if n==0, start is returned
struct DLinkedList* dlinkedlist_next_n(struct DLinkedList *start, size_t n);

// will lookup the nth node before start, if n==0, start is returned
struct DLinkedList* dlinkedlist_prev_n(struct DLinkedList *start, size_t n);

// Will find the end of the list and append data to it
void dlinkedlist_append(struct DLinkedList *cursor, struct DLinkedList *data);

// Will find the start of the list and will prepend data to it
void dlinkedlist_prepend(struct DLinkedList *cursor, struct DLinkedList *data);

// Will insert data after the cursor
void dlinkedlist_insert(struct DLinkedList *cursor, struct DLinkedList *data);

// Will insert data before the cursor
void dlinkedlist_insert_before(struct DLinkedList *cursor, struct DLinkedList *data);

#endif
