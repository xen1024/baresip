#include <stdlib.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>

// MAPI [

bool hash_cmp_handler(struct le *le, void *arg);

bool hash_cmp_handler(struct le *le, void *arg)
{
    (void)le;
    (void)arg;
	return true;
//	return le->data == arg;
}

// map alloc int : ptr
int mapi_alloc(struct hash **map)
{
	int err = hash_alloc(map, 32);
	if (err) {
        re_fprintf(stderr, "hash_alloc failed (%m)\n", err);
        return err;
    }
	return err;
}

// insert into the map
void mapi_insert(struct hash *map, uint32_t key, void *val, struct le *element)
{
    hash_append(map, key, element, val);
}

// map get value for key
struct le *mapi_get(struct hash *map, uint32_t key)
{
	struct le *found = hash_lookup(map, key, hash_cmp_handler, &key);
	return found;
}

// map release
void mapi_release(struct hash *map)
{
	mem_deref(map);
}

// MAPI ]
