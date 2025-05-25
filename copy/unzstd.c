#include <stdio.h>
#include "unzstd.h"

#if 0
void zstd_compress_and_write(struct rw *dst, const void *data, size_t size, uint64_t offset,
    synch_write_op_t write_op);
zstd_buf zstd_compress(void * src, size_t size) {
  size_t const dstCapacity = ZSTD_compressBound(size);
  if (ZSTD_isError(dstSize)) {
    perror("ZSTD_compressBound");
    exit(EXIT_FAILURE);
  }
  void *dst = malloc(dstCapacity);
  if (!dst) {
    perror("compress malloc");
    exit(EXIT_FAILURE);
  }
  size_t const written = ZSTD_compress(dst, dstCapacity, src, size, COMPRESSION_LEVEL);
  if (ZSTD_isError(ret)) {
    perror("ZSTD_compress");
    exit(EXIT_FAILURE);
  }
  zstd_buf ret = {
    .buf = dst,
    .size = written
  }
  zstd_buf ret;
  return ret;
}
#endif

size_t zstd_getBufSize(size_t srcSize, int add) {
  return ZSTD_compressBound(srcSize) + add;
}

void zstd_test() {
  fprintf(stderr, "zstd debug ZSTD_compressBound(%lu) = %lu\n", 64lu, ZSTD_compressBound(64));
}
