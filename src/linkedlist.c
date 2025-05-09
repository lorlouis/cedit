#include "linkedlist.h"

struct DLinkedList* dlinkedlist_next_n(struct DLinkedList *start, size_t n) {
    for(size_t count = 0; count < n; count++) {
        if(start == 0) return 0;
        start = start->next;
    }
    return start;
}

struct DLinkedList* dlinkedlist_prev_n(struct DLinkedList *start, size_t n) {
    for(size_t count = 0; count < n; count++) {
        if(start == 0) return 0;
        start = start->prev;
    }
    return start;
}

// Will find the end of the list and append data to it
void dlinkedlist_append(struct DLinkedList *cursor, struct DLinkedList *data) {
    while(cursor->next != 0) cursor = cursor->next;
    cursor->next = data;
    data->prev = cursor;
}

// Will find the start of the list and will prepend data to it
void dlinkedlist_prepend(struct DLinkedList *cursor, struct DLinkedList *data) {
    while(cursor->prev != 0) cursor = cursor->prev;
    cursor->prev = data;
    data->next = cursor;
}

// Will insert data after the cursor
void dlinkedlist_insert(struct DLinkedList *cursor, struct DLinkedList *data) {
    struct DLinkedList *next = cursor->next;
    if(next) {
        next->prev = data;
    }
    data->next = next;
    data->prev = cursor;
    cursor->next = data;
}

// Will insert data before the cursor
void dlinkedlist_insert_before(struct DLinkedList *cursor, struct DLinkedList *data) {
    struct DLinkedList *prev = cursor->prev;
    if(prev) {
        prev->next = data;
    }
    data->next = cursor;
    data->prev = prev;
    cursor->prev = data;
}

#ifdef TESTING

#include "tests.h"

TESTS_START

TEST_DEF(test_offset)
    struct C {
        int a;
        struct DLinkedList ll;
    } v;

    // assert that the container of macro works
    TEST_ASSERT(container_of(&v.ll, struct C, ll) == &v);
TEST_ENDDEF

TESTS_END

#endif
