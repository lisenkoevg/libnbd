#include "unzstd.h"
#include <stdio.h>

#include "../../nbd/experiments/dump_buffer.c"

void zstd_compress_and_write (struct rw *dst, const void *data, size_t size, uint64_t offset,
    synch_write_op_t write_op) {

  // original (non-compressed) data parameters
  struct {
    size_t size;
    uint64_t offset;
  } const orig = { size, offset };

  // prepare result buffer containing original data parameters + zstd frame
  size_t zstd_compressBound = ZSTD_compressBound(size);
  size_t buf_size = sizeof orig + zstd_compressBound;
  unsigned char *buf_res = malloc (buf_size);
  if (buf_res == NULL) {
    perror("zstd frame malloc");
    exit(EXIT_FAILURE);
  }
  void *zstd_frame_start = buf_res + sizeof orig;
  size_t const ret = ZSTD_compress (zstd_frame_start, zstd_compressBound, data, size, COMPRESSION_LEVEL);
  if (ZSTD_isError(ret)) {
    perror(ZSTD_getErrorName(ret));
    exit(EXIT_FAILURE);
  }
  size_t buf_size_compressed = ret + sizeof orig;
  memcpy (buf_res, (void *) &orig, sizeof orig);
#if 0
  dump_buffer(buf_res, buf_size_compressed);
#endif
  // discard original offset
  write_op (dst, buf_res, buf_size_compressed, 0);
  free (buf_res);
}
