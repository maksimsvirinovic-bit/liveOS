#include "vma.h"
#include "heap.h"
#include <serial/serial.h>
#include <stddef.h>

void vma_list_init(VMAList* list) {
    list->head = NULL;
    list->count = 0;
}

void vma_list_destroy(VMAList* list) {
    VMA* cur = list->head;
    while (cur) {
        VMA* next = cur->next;
        kfree(cur);
        cur = next;
    }
    list->head = NULL;
    list->count = 0;
}

VMA* vma_find(VMAList* list, uint64_t addr) {
    for (VMA* v = list->head; v; v = v->next) {
        if (addr >= v->start && addr < v->end) return v;
    }
    return NULL;
}

VMA* vma_find_overlap(VMAList* list, uint64_t start, uint64_t end) {
    for (VMA* v = list->head; v; v = v->next) {
        if (v->start < end && start < v->end) return v;
    }
    return NULL;
}

uint64_t vma_find_free(VMAList* list, uint64_t lo, uint64_t hi, uint64_t size) {
    if (size == 0 || lo >= hi) return 0;

    uint64_t cursor = lo;

    for (VMA* v = list->head; v; v = v->next) {
        if (v->end <= cursor) continue;
        if (v->start >= hi) break;

        if (v->start > cursor) {
            uint64_t gap = v->start - cursor;
            if (gap >= size) return cursor;
        }
        cursor = v->end > cursor ? v->end : cursor;
        if (cursor >= hi) return 0;
    }

    if (hi - cursor >= size) return cursor;
    return 0;
}

int vma_add(VMAList* list, uint64_t start, uint64_t end,
            uint64_t flags, VMAType type) {
    if (start >= end) return -1;

    if (vma_find_overlap(list, start, end)) {
        serial_emergency_print("vma_add: overlap\n");
        return -1;
    }

    VMA* v = (VMA*)kmalloc(sizeof(VMA));
    if (!v) return -1;

    v->start = start;
    v->end = end;
    v->flags = flags;
    v->type = type;
    v->file_data = NULL;
    v->file_offset = 0;
    v->file_size = 0;

    VMA** pp = &list->head;
    while (*pp && (*pp)->start < start) pp = &(*pp)->next;
    v->next = *pp;
    *pp = v;
    list->count++;

    return 0;
}

int vma_remove(VMAList* list, uint64_t start, uint64_t end) {
    if (start >= end) return -1;

    VMA** pp = &list->head;
    while (*pp) {
        VMA* v = *pp;

        if (v->end <= start || v->start >= end) {
            pp = &v->next;
            continue;
        }

        int head_keep = v->start < start;
        int tail_keep = v->end > end;

        if (head_keep && tail_keep) {
            VMA* tail = (VMA*)kmalloc(sizeof(VMA));
            if (!tail) return -1;
            *tail = *v;
            tail->start = end;
            tail->next = v->next;

            v->end = start;
            v->next = tail;
            list->count++;
            pp = &tail->next;
        } else if (head_keep) {
            v->end = start;
            pp = &v->next;
        } else if (tail_keep) {
            v->start = end;
            pp = &v->next;
        } else {
            *pp = v->next;
            kfree(v);
            list->count--;
        }
    }
    return 0;
}

void vma_dump(VMAList* list) {
    serial_emergency_print("VMA list (");
    serial_emergency_print_hex(list->count);
    serial_emergency_print(" regions):\n");
    for (VMA* v = list->head; v; v = v->next) {
        serial_emergency_print("  [");
        serial_emergency_print_hex(v->start);
        serial_emergency_print("..");
        serial_emergency_print_hex(v->end);
        serial_emergency_print(") flags=");
        serial_emergency_print_hex(v->flags);
        serial_emergency_print(" type=");
        serial_emergency_print_hex(v->type);
        serial_emergency_print("\n");
    }
}