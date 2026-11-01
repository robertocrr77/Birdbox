#include <stdlib.h>
#include "utils/helix_memory.h"

void *helix_malloc(int size)
{
    return size > 0 ? malloc((size_t)size) : NULL;
}

void helix_free(void *ptr)
{
    free(ptr);
}
