#include <stdio.h>
#include <stdlib.h>
#include <zstd.h>
#include <unistd.h>

#include "unzstd.h"

static size_t zstd_prepare_buffer(size_t size, uint64_t offset, void **buf_res);
static size_t zstd_compress(const void *data, size_t size, void **buf_res, size_t buf_size);

void zstd_compress_and_synch_write (struct rw *dst, const void *data, size_t size, uint64_t offset,
    synch_write_op_t synch_write_op) {

  void *buf_res;
  size_t buf_size = zstd_prepare_buffer(size, offset, &buf_res);
  size_t buf_size_compressed = zstd_compress(data, size, &buf_res, buf_size);

  // keep [not used] original offset
  synch_write_op (dst, buf_res, buf_size_compressed, offset);
  free (buf_res);
}

void zstd_compress_and_asynch_write(struct rw *dst, struct command *command, nbd_completion_callback cb,
    asynch_write_op_t asynch_write_op) {

  // TODO: check __thread portability
  static struct command __thread *command_replaced;

  size_t size = command->slice.len;
  void *buf_res;
  size_t buf_size = zstd_prepare_buffer(command->slice.len, command->offset, &buf_res);
  size_t buf_size_compressed = zstd_compress(slice_ptr(command->slice), size, &buf_res, buf_size);

  // keep [not used] original offset
  command_replaced = create_command(command->offset, 0, false, command->worker);

  // free zero-sized allocated block, otherwise valgrind would report
  // "definitely lost: 0 bytes in N blocks"
  free(command_replaced->slice.buffer->data);
  command_replaced->slice.buffer->data = buf_res;
  command_replaced->slice.len = buf_size_compressed;

  asynch_write_op(dst, command_replaced, cb);

  // original *command will be freed upon cb() invocation
  // usleep(1000); probable bug
  free_command(command_replaced);
}

size_t zstd_compress(const void *data, size_t size, void **buf_res, size_t buf_size) {

  void *zstd_frame_start = *buf_res + sizeof(struct zstd_params);
  size_t zstd_frame_len = buf_size - sizeof(struct zstd_params);

  size_t const ret = ZSTD_compress (zstd_frame_start, zstd_frame_len, data, size, COMPRESSION_LEVEL);
  if (ZSTD_isError(ret)) {
    perror(ZSTD_getErrorName(ret));
    exit(EXIT_FAILURE);
  }
  size_t buf_size_compressed = ret + sizeof(struct zstd_params);
  return buf_size_compressed;
}

size_t zstd_prepare_buffer(size_t size, uint64_t offset, void **buf_res) {
  struct zstd_params zstd_params = {
    .original_size = size,
    .original_offset = offset,
  };
  // prepare result buffer containing original buffer parameters + zstd frame
  size_t zstd_compressBound = ZSTD_compressBound(size);
  size_t buf_size = sizeof zstd_params + zstd_compressBound;
  *buf_res = malloc (buf_size);
  if (*buf_res == NULL) {
    perror("zstd buffer malloc");
    exit(EXIT_FAILURE);
  }
  memcpy (*buf_res, (void *) &zstd_params, sizeof zstd_params);
  return buf_size;
}
