#include <zstd.h>
#include <stdint.h>
#include <stdlib.h>

#include "nbdcopy.h"

#define COMPRESSION_LEVEL 3

typedef void (*synch_write_op_t)(struct rw *rw, const void *data, size_t len, uint64_t offset);

void zstd_compress_and_write(struct rw *dst, const void *data, size_t size, uint64_t offset,
    synch_write_op_t synch_write_op);

