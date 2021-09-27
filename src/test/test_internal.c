/*
The MIT License (MIT)

Copyright (c) 2017 Bryan Karr

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/


#include <assert.h>
#include <string.h>
#include <sys/mman.h>

#include "shared_int.h"

static void test_CAS(void)
{
    long original = 1;
    long prev = 1;
    long next = 2;
    assert(CAS(&original, &prev, next));
    assert(prev == 1);
    assert(original == 2);
    assert(!CAS(&original, &prev, next));
}


static void test_DWCAS(void)
{
    DWORD original = {.low = 1, .high = 2};
    DWORD prev = {.low = 1, .high = 2};
    DWORD next = {.low = 3, .high = 4};
    assert(DWCAS(&original, &prev, next));
    assert(prev.low == 1);
    assert(prev.high == 2);
    assert(original.low == 3);
    assert(original.high == 4);
    assert(!DWCAS(&original, &prev, next));
}

static void test_creation(void)
{
    shr_base_s *base = NULL;
    size_t size = 1;

    shm_unlink("basetest");
    assert(validate_name("basetest") == SH_OK);
    assert(validate_name(NULL) == SH_ERR_PATH);
    assert(validate_name("") == SH_ERR_PATH);
    assert(validate_existence("test", NULL) == SH_ERR_EXIST);
    assert(validate_existence("test", &size) == SH_ERR_EXIST);
    assert(size == 0);
    assert(create_base_object(&base, 0, "basetest", "test", 4, 1) == SH_ERR_ARG);
    assert(create_base_object(&base, sizeof(shr_base_s), NULL, "test", 4, 1) == SH_ERR_ARG);
    assert(create_base_object(&base, sizeof(shr_base_s), "basetest", NULL, 4, 1) == SH_ERR_ARG);
    assert(create_base_object(&base, sizeof(shr_base_s), "basetest", "test", 0, 1) == SH_ERR_ARG);
    assert(create_base_object(&base, sizeof(shr_base_s), "basetest", "test", 4, 1) == SH_OK);
    assert(base != NULL);
    assert(validate_existence("basetest", &size) == SH_OK);
    assert(size == PAGE_SIZE);
    assert(base->current != NULL);
    assert(base->prev == base->current);
    assert(base->current->array != NULL);
    assert(memcmp(base->current->array, "test", 4) == 0);
    assert(memcmp(base->name, "basetest", 8) == 0);
    assert(base->current->size == PAGE_SIZE);
    assert(base->current->slots == PAGE_SIZE >> SZ_SHIFT);
    shm_unlink("basetest");
}


void test_expansion(void)
{
    shr_base_s *base = NULL;
    size_t size = 1;

    shm_unlink("basetest");
    assert(create_base_object(&base, sizeof(shr_base_s), "basetest", "test", 4, 1) == SH_OK);
    assert(validate_existence("basetest", &size) == SH_OK);
    assert(size == PAGE_SIZE);
    view_s view = expand(base, base->current, 1000);
    assert(view.status == SH_OK);
    assert(validate_existence("basetest", &size) == SH_OK);
    assert(size > PAGE_SIZE);
    shm_unlink("basetest");

}


void test_flags(void)
{
    shr_base_s *base = NULL;
    shm_unlink("basetest");
    assert(create_base_object(&base, sizeof(shr_base_s), "basetest", "test", 4, 1) == SH_OK);
    assert(base->current->array[FLAGS] == 0);
    assert(set_flag(base->current->array, 1));
    assert(base->current->array[FLAGS] == 1);
    assert(set_flag(base->current->array, 1) == false);
    assert(clear_flag(base->current->array, 1));
    assert(base->current->array[FLAGS] == 0);
    assert(clear_flag(base->current->array, 1) == false);
    shm_unlink("basetest");
}


static void test_alloc_idx_slots(void)
{
    shr_base_s *base = NULL;
    shm_unlink("basetest");
    assert(create_base_object(&base, sizeof(shr_base_s), "basetest", "test", 4, 1) == SH_OK);
    init_data_allocator(base, BASE);
    view_s view = alloc_idx_slots(base);
    assert(view.slot > 0);
    add_end(base, view.slot, FREE_TAIL);
    long first = view.slot;
    view = alloc_idx_slots(base);
    assert(view.slot > 0);
    add_end(base, view.slot, FREE_TAIL);
    view = alloc_idx_slots(base);
    assert(view.slot > 0);
    assert(view.slot == first);
    add_end(base, view.slot, FREE_TAIL);
    shm_unlink("basetest");
}

static void test_free_data_array4(long *array)
{
    sh_status_e status;
    long slot[4];
    shr_base_s *base = NULL;
    shm_unlink("basetest");
    assert(create_base_object(&base, sizeof(shr_base_s), "basetest", "test", 4, 1) == SH_OK);
    init_data_allocator(base, BASE);
    view_s view = alloc_data_slots(base, array[0]);
    slot[0] = view.slot;
    assert(slot[0] > 0);
    view = alloc_data_slots(base, array[1]);
    slot[1] = view.slot;
    assert(slot[1] > 0);
    view = alloc_data_slots(base, array[2]);
    slot[2] = view.slot;
    assert(slot[2] > 0);
    view = alloc_data_slots(base, array[3]);
    slot[3] = view.slot;
    assert(slot[3] > 0);
    status = free_data_slots(base, slot[0]);
    assert(status == SH_OK);
    status = free_data_slots(base, slot[1]);
    assert(status == SH_OK);
    status = free_data_slots(base, slot[2]);
    assert(status == SH_OK);
    status = free_data_slots(base, slot[3]);
    assert(status == SH_OK);
    view = alloc_data_slots(base, array[0]);
    assert(view.slot == slot[0]);
    view = alloc_data_slots(base, array[1]);
    assert(view.slot == slot[1]);
    view = alloc_data_slots(base, array[2]);
    assert(view.slot == slot[2]);
    view = alloc_data_slots(base, array[3]);
    assert(view.slot == slot[3]);
    shm_unlink("basetest");
}

static void test_free_data_slots(void)
{
    long test1[4] = {8, 16, 32, 64};
    test_free_data_array4(test1);
    long test2[4] = {64, 32, 16, 8};
    test_free_data_array4(test2);
    long test3[4] = {64, 16, 8, 32};
    test_free_data_array4(test3);
    long test4[4] = {64, 8, 32, 16};
    test_free_data_array4(test4);
    long test5[4] = {8, 64, 16, 32};
    test_free_data_array4(test5);
}

static void test_first_fit_allocation(void)
{
    sh_status_e status;
    long biggest_slot = 0;
    long bigger_slot = 0;
    view_s view;
    shr_base_s *base = NULL;
    shm_unlink("basetest");
    assert(create_base_object(&base, sizeof(shr_base_s), "basetest", "test", 4, 1) == SH_OK);
    init_data_allocator(base, BASE);
    view = alloc_data_slots(base, 64);
    biggest_slot = view.slot;
    assert(biggest_slot > 0);
    view = alloc_data_slots(base, 32);
    bigger_slot = view.slot;
    assert(bigger_slot > 0);
    status = free_data_slots(base, biggest_slot);
    assert(status == SH_OK);
    status = free_data_slots(base, bigger_slot);
    assert(status == SH_OK);
    view = alloc_data_slots(base, 20);
    assert(view.slot == bigger_slot);
    view = alloc_data_slots(base, 20);
    assert(view.slot == biggest_slot);
    shm_unlink("basetest");
    assert(create_base_object(&base, sizeof(shr_base_s), "basetest", "test", 4, 1) == SH_OK);
    init_data_allocator(base, BASE);
    view = alloc_data_slots(base, 64);
    biggest_slot = view.slot;
    assert(biggest_slot > 0);
    view = alloc_data_slots(base, 32);
    bigger_slot = view.slot;
    assert(bigger_slot > 0);
    view = alloc_data_slots(base, 16);
    assert(view.slot != 0);
    status = free_data_slots(base, view.slot);
    assert(status == SH_OK);
    status = free_data_slots(base, biggest_slot);
    assert(status == SH_OK);
    status = free_data_slots(base, bigger_slot);
    assert(status == SH_OK);
    view = alloc_data_slots(base, 20);
    assert(view.slot == bigger_slot);
    assert(view.extent->array[view.slot] == 32);
    view = alloc_data_slots(base, 20);
    assert(view.slot == biggest_slot);
    assert(view.extent->array[view.slot] == 64);
    view = alloc_data_slots(base, 20);
    assert(view.extent->array[view.slot] == 32);
    view = alloc_data_slots(base, 20);
    assert(view.extent->array[view.slot] == 32);
    shm_unlink("basetest");
}

static void test_large_data_allocation(void)
{
    sh_status_e status;
    long big_slot = 0;
    long bigger_slot = 0;
    long slot = 0;
    shr_base_s *base = NULL;
    shm_unlink("basetest");
    assert(create_base_object(&base, sizeof(shr_base_s), "basetest", "test", 4, 1) == SH_OK);
    init_data_allocator(base, BASE);
    view_s view = alloc_data_slots(base, 4096 >> SZ_SHIFT);
    big_slot = view.slot;
    assert(big_slot > 0);
    status = free_data_slots(base, big_slot);
    assert(status == SH_OK);
    view = alloc_data_slots(base, 8192 >> SZ_SHIFT);
    bigger_slot = view.slot;
    assert(bigger_slot > 0);
    status = free_data_slots(base, bigger_slot);
    assert(status == SH_OK);
    view = alloc_data_slots(base, 4096 >> SZ_SHIFT);
    slot = view.slot;
    assert(slot > 0);
    assert(big_slot == slot);
    status = free_data_slots(base, big_slot);
    assert(status == SH_OK);
    view = alloc_data_slots(base, 8192 >> SZ_SHIFT);
    slot = view.slot;
    assert(slot > 0);
    assert(bigger_slot == slot);
    view = alloc_data_slots(base, 4096 >> SZ_SHIFT);
    slot = view.slot;
    assert(slot > 0);
    assert(big_slot == slot);
    shm_unlink("basetest");
}

int main(void)
{
	test_CAS();
	test_DWCAS();
    test_creation();
    test_expansion();
    test_flags();
    test_alloc_idx_slots();
    test_free_data_slots();
    test_first_fit_allocation();
    test_large_data_allocation();

    return 0;
}
