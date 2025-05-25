#include <zstd.h>
#include <nbdcopy.h>

#define COMPRESSION_LEVEL 3

typedef struct zstd_buf {
  void *buf;
  size_t size;
} zstd_buf;

// typedef void (*synch_write_op_t)(struct rw *rw, const void *data, size_t len, uint64_t offset);

// void zstd_compress_and_write(struct rw *dst, const void *data, size_t size, uint64_t offset,
//     synch_write_op_t write_op);
size_t zstd_getBufSize(size_t srcSize, int add);

void zstd_test();

