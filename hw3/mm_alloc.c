#include "mm_alloc.h"
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>


s_block_ptr head_pointer = NULL;


/*
 * In this function, we check that if the size of the fre block 
 * is greater than or equal to the required size, we split it.
 */
void split_block (s_block_ptr block, size_t size)
{
    if (block == NULL || size <= 0)
        return;

    if(block->size >= size + sizeof(s_block)) {
        s_block_ptr ptr = (s_block_ptr) (block->ptr + size);
        ptr->prev = block;
        ptr->next = block->next;
        ptr->size = block->size - size - sizeof(s_block);

        if (block->next)
            (block->next)->prev = ptr;

        block->next = ptr;
        ptr->ptr = block->ptr + size + sizeof(s_block);
        block->size = size;

        mm_free(ptr->ptr);
        memset(block->ptr, 0, block->size);
    }
}

/*
 * In this function, we try to create a new block and we do this using the sbrk system call.
 */
s_block_ptr extend_heap (s_block_ptr last , size_t s)
{
    void *ptr = sbrk(s + sizeof(s_block));

    if (ptr == (void *) -1)
        return NULL;

    s_block_ptr block = (s_block_ptr) ptr;

    if (last)
        last->next = block;
    else
        head_pointer = block;

    block->next = NULL;
    block->prev = last;
    block->is_free = 0;
    block->size = s;
    block->ptr = ptr + sizeof(s_block);

    memset(block->ptr, 0, block->size);

    return block->ptr;
}

s_block_ptr get_block (void *ptr)
{
    for (s_block_ptr head = head_pointer; head; head = head->next) {
        if (head->ptr == ptr)
            return head;
    }
    return NULL;
}

/*
 * In this function, we check that if the blocks before and after a block were fre, 
 * we merge them with the block itself to use the optimal memory.
 */
void fusion(s_block_ptr block)
{
    if (block->next != NULL && (block->next)->is_free) {
        block->next = (block->next)->next;
        (block->next)->prev = block;
        block->size = block->size + sizeof(s_block) +(block->next)->size;
    }

    if (block->prev != NULL && (block->prev)->is_free) {
        (block->prev)->next = block->next;
        if (block->next != NULL)
            (block->next)->prev = block->prev;
        (block->prev)->is_free = block->is_free;
        (block->prev)->size = (block->prev)->size + sizeof(s_block) + block->size;
    }

}


/*
 * In this function, we create a block for head of list.
 */
s_block_ptr initial_heap (size_t s)
{
    void *ptr = sbrk(s + sizeof(s_block));

    if (ptr == (void *) -1)
        return NULL;

    head_pointer = (s_block_ptr) ptr;

    head_pointer->next = NULL;
    head_pointer->prev = NULL;
    head_pointer->is_free = 0;
    head_pointer->size = s;
    head_pointer->ptr = ptr + sizeof(s_block);

    memset(head_pointer->ptr, 0, head_pointer->size);

    return head_pointer->ptr;
}


void* mm_malloc(size_t size)
{
    if (size == 0)
        return NULL;

    if (head_pointer == NULL)
        return initial_heap(size);

    s_block_ptr prev = NULL;

    for (s_block_ptr head = head_pointer; head; head = head->next)
    {
        if (head->is_free == 1 && head->size >= size) {
            split_block(head, size);
			head->is_free = 0;
            return head->ptr;
        }
        prev = head;
    }

    return extend_heap(prev, size);
}


void* mm_realloc(void* ptr, size_t size)
{
    if (size == 0)
        return NULL;

    if (ptr == NULL)
        return mm_malloc(size);

    s_block_ptr block = get_block(ptr);
    if (block){
        void *new = mm_malloc(size);

        if (size > block->size) {
            memcpy(new, ptr, block->size);
            block->is_free = 1;
            fusion(block);
            return new;
        }

        memcpy(new, ptr, size);
        block->is_free = 1;
        fusion(block);
        return block->ptr;
        
    }
    return NULL;

}


void mm_free(void* ptr)
{
    if (ptr == NULL)
        return;
    
    s_block_ptr block = get_block(ptr);
    
    if(block){
        block->is_free = 1;
        fusion(block);
    }
}
