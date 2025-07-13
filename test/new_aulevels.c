/**
 * @file test/new_aulevels.c  Baresip selftest -- aulevels module
 *
 * Copyright (C) 2025 Victor Kozub
 */

#include <string.h>
#include <re.h>
#include <baresip.h>
#include "test.h"
//#include "../modules/aulevels/aulevels.h"
#include "../modules/aulevels/aulevels.c"

int run_mapi(void);
int run_hash(void);

int test_mapi(void);

static struct hash *my_map;

// TESTS [
// MAPI RUN [

int run_mapi(void)
{
	int err = 0;

	// New map
	mapi_alloc(&my_map);

	// Create first map element for key=123
	uint32_t key = 0;
    void *val = (void *)123;
	struct le element;
	mapi_insert(my_map, key, val, &element);

	// Create first map element for key=1223355
	uint32_t key1 = 1223355;
	struct le *element1 = mapi_insert_alloc(my_map, key1, val);
	ASSERT_TRUE(element1);

	// Dump map
	for (uint i = 0; i < hash_bsize(my_map); i++) {
		struct list *hlist = hash_list_idx(my_map, i);
		ASSERT_TRUE(hlist);

		for (struct le *le = list_head(hlist); le; le = le->next) {
			struct le *item = le->data;
			ASSERT_TRUE(item);

			void *data = item->data;

			printf("%p %i\n", (void *)le, (int)data);
		}
	}

	// Get key=123
	struct le *found = mapi_get(my_map, key);
    if (found) {
        printf("Correct! Found: %i -> %p\n", key, (char *)found->data);
    }

	// Delete key=123
	mapi_delete(my_map, key);

	// Get key=123 must be NULL
	found = mapi_get(my_map, key);
    if (!found) {
        printf("Correct! Not found: %i\n", key);
    }

	// Release map
	mapi_release(my_map);
out:
	return err;
}

// MAPI RUN ]
// RUN HASH [

int run_hash(void)
{
	// map_alloc
	int err = hash_alloc(&my_map, 32);
	if (err) {
        re_fprintf(stderr, "hash_alloc failed (%m)\n", err);
        return err;
    }

	// map_insert
	// Insert into the map
    uint32_t key = 0;
    void *val = (void *)123;
	struct le element;
    hash_append(my_map, key, &element, &val);

	// map_get
	struct le *found = hash_lookup(my_map, key, hash_cmp_handler, &key);
    if (found) {
        printf("Found: %i -> %p\n", key, (char *)found->data);
    }

	// map_free
	mem_deref(my_map);
	return err;
}

// RUN HASH ]
// TEST MAPI [

int test_mapi(void)
{
	int err = 0;

    run_mapi();
    run_hash();

	goto out;

out:
    return err;
}

int test_aulevels(void)
{
	int err = 0;

   	err = module_load(".", "aulevels");
	TEST_ERR(err);

    err = test_mapi();
	TEST_ERR(err);

out:
	return err;
}


