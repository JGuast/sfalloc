#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "sfmm.h"

/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */

sf_free_header* freelist_head = NULL;

void* lower_limit;
void* current_position; // current position of the
int brk_count = 0; // number of times sf_sbrk has been called with a nonzero arg
sf_header* header;
sf_footer* footer;

// values for sf_info
static size_t internal = 0; // done
static size_t external = 0; // done
static size_t allocations = 0; // done
static size_t frees = 0; // done
static size_t coalesce = 0; // done 

// SHOULD RETURN AN ADDRESS DIVISIBLE BY 16
void *sf_malloc(size_t size)
{
	if (size == 0)
	{
		errno = EINVAL;
		return NULL;
	}

	if (size > 16368) // if size > 4 pages (-16 head/foot)
	{
		errno = ENOMEM;
		return NULL;
	}

	// Find the required padding
	int padding = 0;
	if (size % 16 != 0)
		padding = 16 - (size % 16);

	// If it's the first malloc
	if (brk_count == 0) // if (brk_count == 0)
	{
		current_position = sf_sbrk(0); // store current position before moving heap pointer
		lower_limit = current_position;
		sf_sbrk(1); // Call sf_sbrk(1) 
		external += 4096;
		brk_count++;
		
		// Create the header at current position
		header = (sf_header*) current_position ;
		header->alloc = 1; // alloc : 4
		header->block_size = (size + padding + 16) >>4; // block_size : 28
    	header->padding_size = padding; // padding_size : 4
		
		// Create the footer
		footer = (sf_footer*) (current_position + (header->block_size << 4) - 8);
		footer->alloc = 1; // alloc : 4
		footer->block_size = (size + padding + 16) >> 4; // block_size : 28

		// Create a pointer to the free block
		freelist_head = (sf_free_header*) (current_position + (header->block_size << 4));
		header = (sf_header*) &(freelist_head->header); // header now points to freelist_head->header
		header->alloc = 0; // alloc is 0 for freed block
		header->block_size = (sf_sbrk(0) - ((void*)freelist_head)) >>4; // position of heap ptr - start of header 
		header->padding_size = 0; // padding size for free block is 0 as per piazza
		freelist_head->next = NULL; // next 
		freelist_head->prev = NULL; // prev

		// append a footer
		footer = ((void*)freelist_head) + (header->block_size << 4) - 8;
		footer->alloc = 0; // alloc is 0 for freed block
		footer->block_size = header->block_size; // block size is same as header

		allocations++;
		internal += padding;
		external -= (header->block_size<<4);
		frees++; // The block was split and part of it was "freed"

		return current_position + 8; // pointer to allocated payload
	}
	
	// Check for a block in free list
	if (freelist_head != NULL)
	{
		int free_found = 0;
		sf_free_header* freelist_cursor = freelist_head;
		sf_header* search_head;
		// Search the free list for the first block that is large enough
		while (freelist_cursor != NULL)
		{
			search_head = (sf_header*) &(freelist_cursor->header);
			// if payload of free block >= requested size
			// if block_size - (size+padding+16) >= 32
			if (((search_head->block_size << 4)-16) >= size) // padding is 0, so payload = (block << 4) - 16(headers)
			//if (((search_head->block_size<<4) - (size + padding + 16)) >= 32)
			{
				int i_size = ((int)(search_head->block_size<<4));
				if ((i_size - (size + padding + 16)) >= 32)
				{
					// The block is large enough
					// get the next and previous headers
					sf_free_header* next_block = freelist_cursor->next;
					sf_free_header* prev_block = freelist_cursor->prev;
					
					// adjust next/prev accordingly to remove block from freelist
					if (next_block != NULL)
						next_block->prev = freelist_cursor->prev;
					if (prev_block != NULL)
						prev_block->next = freelist_cursor->next;
					else 
						freelist_head = freelist_cursor->next; // prev = null so it's the head

					// Set the next and prev pointers to NULL
					freelist_cursor->next = NULL;
					freelist_cursor->prev = NULL;

					// current_position = free block to use
					current_position = (void*)freelist_cursor;
					free_found = 1;

					internal += (padding + 16); 
					break; // break loop
				}
				else if ((i_size - (size + padding + 16)) == 0) 
				{
					// Remove from freelist
					sf_free_header* search_free = (sf_free_header*) search_head; // free header for block to remove
					if (search_free->next != NULL)
						search_free->next->prev = search_free->prev;
					if (search_free->prev != NULL)
						search_free->prev->next = search_free->next;
					else 
						freelist_head = search_free->next; // search_free is the head of the list

					search_free->next = NULL;
					search_free->prev = NULL;

					search_head->alloc = 1; // Change header alloc
					search_head->padding_size = padding; // Change header padding_size
					// block size is already correct 
					sf_footer* search_foot = (sf_footer*) (((void*)search_head) + (search_head->block_size<<4) -8); // get footer
					search_foot->alloc = 1; // Change footer alloc 
					// block size is already correct

					internal += padding;
					allocations++;
					external -= (header->block_size<<4);
					current_position = (void*)search_head;

					return (current_position + 8); // return address of payload 
				}
			}
			// Fit not yet found, advance cursor
			freelist_cursor = freelist_cursor->next; // while condition takes care of end of list		
		}
		if (free_found == 0)
		{
			// No suitable block on the list
			// check to see if enough pages can be added to complete alloc
			if (brk_count < 4)
			{
				//printf("start of new page --> %p\n", sf_sbrk(0));
				sf_header* heap_top = (sf_header*)(sf_sbrk(0));
				sf_sbrk(1); // add a new page
				external += 4096;
				//printf("end of new page   --> %p", sf_sbrk(0));
				brk_count++; // increment break count

				// initialize the header so it can be freed
				heap_top->padding_size = 0;
				heap_top->alloc = 1;
				heap_top->block_size = (4096/16);
				// set footer info for new page so it can be free'd
				sf_footer* heap_foot = (sf_sbrk(0) - 8); // get footer address
				heap_foot->alloc = 1;
				heap_foot->block_size = heap_top->block_size;
				void* heap_payload = ((void*)heap_top)+8; 
				sf_free(heap_payload); // free it

				return sf_malloc(size); // return malloc(size);
			}
			else 
			{
				errno = ENOMEM;
				return NULL;
			}
		} // Else free block has been found and returned
	}
	else // Else, freelist is empty
	{

		// If another page needed
		if (brk_count < 4)
		{
			current_position = sf_sbrk(0); // since free_list is empty, sf_brk(0) must be the current position
			// Add a new page, and continue
			sf_sbrk(1);
			external += 4096;
			brk_count++;
 			// set the header block size so it can be used to split the block later NEW 2 lines vvv
 			header = (sf_header*) current_position;
 			header->block_size = (4096 / 16);
		}
		else // Else, return (not enough free space)
		{
			errno = ENOMEM;
			return NULL;
		}

	}
	// At this point, current_position points to a free block that is large enough for the requested size

	// allocate the first part
	header = (sf_header*) current_position;	// get header to allocate
	footer = (sf_footer*) (current_position + size + padding + 8); // get footer to allocate
	sf_header* free_head = (sf_header*) (current_position + size + padding + 16); // Get free header
	sf_footer* free_foot = (sf_footer*) (current_position + (header->block_size<<4) - 8);  // Get free footer

	// block size of free =    (size of whole block   -  size of allocated block) >> 4
	free_head->block_size = (((header->block_size<<4) - (padding + size + 16)) >> 4);// Set free_head block size

	header->alloc = 1; // Set header alloc
	header->padding_size = padding; // Set header padding size
	header->block_size = ((size + padding + 16) >> 4); // Set header block size

	footer->alloc = 1; // Set footer alloc
	footer->block_size = header->block_size; // Set footer block size

	free_head->alloc = 1;
	free_foot->alloc = 1;
	free_foot->block_size = free_head->block_size;

	void* free_head_void = (void*) free_head;
	sf_free((free_head_void+8));

	internal += (padding + 16);
	allocations++;
	external -= (header->block_size<<4);

	return current_position + 8;
}

// does nothing if the address is invalid (not alloc'd)
void sf_free(void *ptr)
{
	if (ptr == NULL)
		return;

	sf_header* hptr = (sf_header*) (ptr - 8); // header of the block starts at ptr -8

	if (hptr->alloc == 0) // if the block referenced isn't alloc'd
		return;

	if (freelist_head == NULL) // DONE
	{
		//printf("EMPTY FREELIST\n");
		internal -= hptr->padding_size;
		external += ((hptr->block_size<<4) -16);
		frees++;
		// The freelist is empty - get the header
		freelist_head = (sf_free_header*) hptr; // cast the hptr to sf_free_header a
		// Set values for header fields
		sf_header* head = (sf_header*) hptr; // pointer for header field
		head->padding_size = 0;
		head->alloc = 0;
		// Set values for footer fields
		sf_footer* foot = (sf_footer*)(ptr + (head->block_size << 4) - 16);
		foot->alloc = 0;
		// Set values for next and prev
		freelist_head->next = NULL; // set next
		freelist_head->prev = NULL; // set prev
	}
	else // There are blocks in the free list
	{
		//printf("BLOCKS IN FREELIST\n");
		internal -= hptr->padding_size;
		external += ((hptr->block_size<<4) -16);
		frees++;

		// current block header --> hptr
		sf_footer* prev = (sf_footer*) (ptr - 16); // Get the address of the payload - 16 to get the footer of previous
		sf_header* next = (sf_header*) (ptr + (hptr->block_size << 4) - 8); // Get the address of the header of the next block
		// Test for the following cases:

		int prev_alloc;
		if ((void*)hptr <= lower_limit) // block is at lowest address, there can't be a previous
			prev_alloc = 1; // treat it as though prev was allocated
		else 
			prev_alloc = prev->alloc;

		int next_alloc;
		if ((void*)next >= sf_sbrk(0))
			next_alloc = 1;
		else 
			next_alloc = next->alloc;

		// 1. Previous and next both allocated
		if (prev_alloc == 1 && next_alloc == 1)
		{
			//printf("BOTH ALLOCATED\n");
			// Just add the block to the head of freelist
			sf_free_header* add_block = (sf_free_header*) hptr;
			hptr->alloc = 0;
			hptr->padding_size = 0;
			sf_footer* foot = (sf_footer*)(ptr + (hptr->block_size << 4) - 16);
			foot->alloc = 0;
			add_block->next = freelist_head;
			add_block->prev = NULL;
			freelist_head->prev = add_block;
			freelist_head = add_block;
		}
		else if (prev_alloc == 1 && next_alloc == 0) // 2. Previous is allocated, next is free
		{
			//printf("PREV ALLOCATED, NEXT FREE\n");
			internal -= 16;
			external += 16;
			coalesce++;
			sf_footer* foot = (sf_footer*) (((void*)next) + (next->block_size << 4) -8); // Get next footer (already have header)
			foot->block_size += hptr->block_size; // Edit next footer block size
			hptr->block_size = foot->block_size; // Edit current header block size
			hptr->padding_size = 0; // Set padding to 0
			hptr->alloc = 0; // Flip current header alloc
			sf_free_header* fhptr = (sf_free_header*) hptr; // create a free header for current
			sf_free_header* fnext = (sf_free_header*) next; // create a free header for next
			// move pointers to preserve the list
			if (fnext->next != NULL)
				fnext->next->prev = fnext->prev;
			if (fnext->prev != NULL)
			{
				fnext->prev->next = fnext->next;
				// append current to head of the list
				fhptr->prev = NULL; // current->prev = NULL
				fhptr->next = freelist_head; // current->next = head
				freelist_head->prev = fhptr; // head->prev = current
				freelist_head = fhptr; // head = current
			}
			else
			{ 
				freelist_head = fnext->next; // next block is head of list, new head is next->next
				fhptr->prev = NULL;
				fhptr->next = freelist_head;
				freelist_head = fhptr;
			}
			 
		}
		else if (prev_alloc == 0 && next_alloc == 1) // 3. Previous is free, next is allocated
		{ 
			//printf("PREV FREE, NEXT ALLOCATED\n");
			internal -= 16;
			external += 16;
			coalesce++;
			sf_header* head = (sf_header*) (((void*)prev)+8 - (prev->block_size << 4)); // Get prev header (already have footer)
			//sf_varprint(ptr); // Current
			//sf_varprint(((void*)head)+8); // Previous
			sf_footer* fcurr = (sf_footer*) (((void*)hptr) + (hptr->block_size << 4) - 8); // get the current footer
			
			head->padding_size = 0;
			head->block_size += hptr->block_size; // Increase block size
			fcurr->block_size = head->block_size; // Edit current footer block size 
			fcurr->alloc = 0; // Edit current footer alloc
			//ssf_free_header* fhptr = (sf_free_header*) hptr; // create a free header for current
			sf_free_header* fhead = (sf_free_header*) head; // create a free header for previous
			// move pointers to preserve the list
			if (fhead->prev != NULL)
			{
				//printf("prev->prev -- %p\n", fhead->prev);
				//printf("prev->next -- %p\n", fhead->next);
				fhead->prev->next = fhead->next;
				if (fhead->next != NULL)
					fhead->next->prev = fhead->prev;
				// append previous to head of the list
				fhead->prev = NULL; // prev->prev = NULL
				fhead->next = freelist_head; // prev->next = head
				freelist_head->prev = fhead; // head->prev = prev
				freelist_head = fhead; // head = prev
			} 
			// else, prev->prev == NULL then it's already the head of the lists
		}
		else // 4. Both free (already taken care of, nigga)
		{
			//printf("BOTH FREE\n");
			internal -= 32;
			external += 32;
			coalesce++;
			// Get the headers/footers you'll need
			sf_header* prev_head = (sf_header*) (((void*)prev)+8 - (prev->block_size << 4)); // Get prev header
			sf_free_header* p_head = (sf_free_header*) prev_head; // Get free header for prev
			sf_free_header* n_head = (sf_free_header*) next; // Get free header for next
			sf_footer* n_foot = (sf_footer*) (((void*)next) + (next->block_size << 4) -8); // Get next footer 
			// next = next header

			// Merge all 3 block sizes
			prev_head->block_size += (hptr->block_size + next->block_size); // head->block_size = sum of all 3 block sizes
			n_foot->block_size = prev_head->block_size; // foot->block_size = head->block_size

			// Remove them from the list
			if (p_head->prev == NULL) // IF prev->prev == NULL
			{
				// THEN prev is head of list
				if (p_head->next == n_head)
				{
					p_head->next = n_head->next;
					if (p_head->next != NULL)
						p_head->next->prev = p_head;
				}
				// Previous is already head of the list, freelist_head == p_head 
			}
			else  // ELSE, prev is not head of list 
			{
				if (p_head->prev != NULL)
					p_head->prev->next =  p_head->next;
				if (p_head->next != NULL)
					p_head->next->prev = p_head->prev;
				// Remove next from the list
				if (n_head->prev != NULL)
					n_head->prev->next = n_head->next;
				if (n_head->next != NULL)
					n_head->next->prev = n_head->prev;

				p_head->prev = NULL; // prev = NULL
				p_head->next = freelist_head; // next = freelist_head
				freelist_head->prev = p_head;
				freelist_head = p_head; 
			}
		} // end of else
	} // end of else
}

void *sf_realloc(void *ptr, size_t size)
{
	if (ptr == NULL || size == 0)
	{
		errno = EINVAL;
		return NULL;
	}

	void* reallocated_block = sf_malloc(size);
	if (reallocated_block == NULL)
	{
		return NULL;
	}
	sf_free(ptr);
  return reallocated_block;
}

int sf_info(info* meminfo)
{
	if (meminfo == NULL)
	{
		errno = EINVAL;
		return -1;
	}
	meminfo->internal = internal;
	meminfo->external = external;
	meminfo->allocations = allocations;
	meminfo->frees = frees;
	meminfo->coalesce = coalesce;
  	return 0;
}