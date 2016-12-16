#include <criterion/criterion.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "sfmm.h"

/**
 *  HERE ARE OUR TEST CASES NOT ALL SHOULD BE GIVEN STUDENTS
 *  REMINDER MAX ALLOCATIONS MAY NOT EXCEED 4 * 4096 or 16384 or 128KB
 */

Test(sf_memsuite, Malloc_an_Integer, .init = sf_mem_init, .fini = sf_mem_fini) {
    int *x = sf_malloc(sizeof(int));
    *x = 4;
    cr_assert(*x == 4, "Failed to properly sf_malloc space for an integer!");
}

Test(sf_memsuite, Free_block_check_header_footer_values, .init = sf_mem_init, .fini = sf_mem_fini) {
    void *pointer = sf_malloc(sizeof(short));
    sf_free(pointer);
    pointer = pointer - 8;
    sf_header *sfHeader = (sf_header *) pointer;
    cr_assert(sfHeader->alloc == 0, "Alloc bit in header is not 0!\n");
    sf_footer *sfFooter = (sf_footer *) (pointer - 8 + (sfHeader->block_size << 4));
    cr_assert(sfFooter->alloc == 0, "Alloc bit in the footer is not 0!\n");
}

Test(sf_memsuite, PaddingSize_Check_char, .init = sf_mem_init, .fini = sf_mem_fini) {
    void *pointer = sf_malloc(sizeof(char));
    pointer = pointer - 8;
    sf_header *sfHeader = (sf_header *) pointer;
    cr_assert(sfHeader->padding_size == 15, "Header padding size is incorrect for malloc of a single char!\n");
}

Test(sf_memsuite, Check_next_prev_pointers_of_free_block_at_head_of_list, .init = sf_mem_init, .fini = sf_mem_fini) {
    int *x = sf_malloc(4);
    memset(x, 0, 4);
    cr_assert(freelist_head->next == NULL);
    cr_assert(freelist_head->prev == NULL);
}

Test(sf_memsuite, Coalesce_no_coalescing, .init = sf_mem_init, .fini = sf_mem_fini) {
    void *x = sf_malloc(4);
    void *y = sf_malloc(4);
    memset(y, 0xFF, 4);
    sf_free(x);
    cr_assert(freelist_head == x-8);
    sf_free_header *headofx = (sf_free_header*) (x-8);
    sf_footer *footofx = (sf_footer*) (x - 8 + (headofx->header.block_size << 4)) - 8;

    sf_blockprint((sf_free_header*)((void*)x-8));
    // All of the below should be true if there was no coalescing
    cr_assert(headofx->header.alloc == 0);
    cr_assert(headofx->header.block_size << 4 == 32);
    cr_assert(headofx->header.padding_size == 0);

    cr_assert(footofx->alloc == 0);
    cr_assert(footofx->block_size << 4 == 32);
}

/*
//############################################
// STUDENT UNIT TESTS SHOULD BE WRITTEN BELOW
// DO NOT DELETE THESE COMMENTS
//############################################
*/

// Allocate 3 blocks of memory, free the first, then free the second
Test(sf_memsuite, Coalesce_right_side, .init = sf_mem_init, .fini = sf_mem_fini) {
    void *a = sf_malloc(4);
    void *b = sf_malloc(4);
    void *c = sf_malloc(4);

    //compiler wants c to be used
    c++;
    c--;

    memset(a, 0xFF, 4);
    sf_free(a);
    cr_assert(freelist_head == a-8);
    sf_free_header *headofa = (sf_free_header*) (a-8);
    sf_footer *footofa = (sf_footer*) (a - 8 + (headofa->header.block_size << 4)) - 8;

    // All of the below should be true if there was no coalescing
    cr_assert(headofa->header.alloc == 0);
    cr_assert(headofa->header.block_size << 4 == 32);
    cr_assert(headofa->header.padding_size == 0);

    cr_assert(footofa->alloc == 0);
    cr_assert(footofa->block_size << 4 == 32);

    sf_free(b);
    // head should be a since it will coalesce
    cr_assert(freelist_head == a-8);

    cr_assert(headofa->header.alloc == 0);
    cr_assert(headofa->header.block_size << 4 == 64);
    cr_assert(headofa->header.padding_size == 0);
}

// Allocate 3 blocks of memory, free the second, then free the first
Test(sf_memsuite, Coalesce_left_side, .init = sf_mem_init, .fini = sf_mem_fini) {
    void *a = sf_malloc(4);
    void *b = sf_malloc(4);
    void *c = sf_malloc(4);

    //compiler wants c to be used
    c++;
    c--;

    memset(b, 0xFF, 4);
    sf_free(b);
    cr_assert(freelist_head == b-8);
    //headofb
    sf_free_header *headofb = (sf_free_header*) (b-8);
    sf_footer *footofb = (sf_footer*) (b - 8 + (headofb->header.block_size << 4)) - 8;
    // headofa
    sf_free_header *headofa = (sf_free_header*) (a-8);

    // All of the below should be true if there was no coalescing
    cr_assert(headofb->header.alloc == 0);
    cr_assert(headofb->header.block_size << 4 == 32);
    cr_assert(headofb->header.padding_size == 0);

    cr_assert(footofb->alloc == 0);
    cr_assert(footofb->block_size << 4 == 32);

    sf_free(a);
    // head should be a, next should be b
    cr_assert(freelist_head == a-8); 

    cr_assert(headofa->header.alloc == 0);
    cr_assert(headofa->header.block_size << 4 == 64);
    cr_assert(headofa->header.padding_size == 0);
}

// Allocate 3 blocks, Free the first and third, then free the second
Test(sf_memsuite, Coalesce_both, .init = sf_mem_init, .fini = sf_mem_fini) {
    void *a = sf_malloc(4);
    void *b = sf_malloc(4);
    void *c = sf_malloc(4);

    memset(a, 0xFF, 4);
    sf_free(a);
    sf_free_header *headofa = (sf_free_header*) (a-8);

    sf_free(c);
    sf_free(b);

    cr_assert(freelist_head == a-8);
    cr_assert(headofa->header.alloc == 0);
    cr_assert(headofa->header.block_size << 4 == 4096);
    cr_assert(headofa->header.padding_size == 0);
}