#include <stdint.h>

#define ptradd(p, o) ((void *)(((char *)(p)) + (o)))

void sha256_hash(void *src, size_t len, uint32_t dest[8]);