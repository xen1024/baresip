#ifndef _AULEVELS_H_

int mapi_alloc(struct hash **map);
void mapi_insert(struct hash *map, uint32_t key, void *val, struct le *element);
struct le *mapi_get(struct hash *map, uint32_t key);
void mapi_release(struct hash *map);
void mapi_test(void);

#endif
