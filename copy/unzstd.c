#include <stdio.h>
#include <stdlib.h>
#include <zstd.h>

#include "unzstd.h"

#if 0
#include "../../nbd/experiments/dump_buffer.c"
#endif

void zstd_compress_and_write (struct rw *dst, const void *data, size_t size, uint64_t offset,
    synch_write_op_t write_op) {

  struct zstd_params zstd_params = {
    .original_size = size,
    .original_offset = offset,
  };

  // prepare result buffer containing original data parameters + zstd frame
  size_t zstd_compressBound = ZSTD_compressBound(size);
  size_t buf_size = sizeof zstd_params + zstd_compressBound;
  unsigned char *buf_res = malloc (buf_size);
  if (buf_res == NULL) {
    perror("zstd frame malloc");
    exit(EXIT_FAILURE);
  }
  void *zstd_frame_start = buf_res + sizeof zstd_params;
  size_t const ret = ZSTD_compress (zstd_frame_start, zstd_compressBound, data, size, COMPRESSION_LEVEL);
  if (ZSTD_isError(ret)) {
    perror(ZSTD_getErrorName(ret));
    exit(EXIT_FAILURE);
  }
  size_t buf_size_compressed = ret + sizeof zstd_params;
  memcpy (buf_res, (void *) &zstd_params, sizeof zstd_params);
#if 0
  dump_buffer(buf_res, buf_size_compressed);
#endif
  // discard original offset
  write_op (dst, buf_res, buf_size_compressed, 0);
  free (buf_res);
}
