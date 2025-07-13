#ifndef _AULEVELS_H_

// MAPI

//#include <re>

int mapi_alloc(struct hash **map);
void mapi_insert(struct hash *map, uint32_t key, void *val, struct le *element);
struct le *mapi_insert_alloc(struct hash *map, uint32_t key, void *val);
struct le *mapi_get(struct hash *map, uint32_t key);
void mapi_delete(struct hash *map, uint32_t key);
void mapi_release(struct hash *map);
void mapi_test(void);

#endif
