#ifndef UNZSTD_H
#define UNZSTD_H

#include "nbdcopy.h"

#define COMPRESSION_LEVEL 3

// non-compressed data parameters
struct zstd_params {
  size_t original_size;
  uint64_t original_offset;
};

// Copied from multi-thread-copying.c
extern struct command *create_command (uint64_t offset, size_t len, bool zero,
                                       struct worker *worker);
extern void free_command (struct command *command);

typedef void (*synch_write_op_t)(struct rw *rw, const void *data, size_t len, uint64_t offset);
typedef void (*asynch_write_op_t)(struct rw *, struct command *, nbd_completion_callback);

void zstd_compress_and_synch_write(struct rw *dst, const void *data, size_t size, uint64_t offset,
    synch_write_op_t synch_write_op);

void zstd_compress_and_asynch_write(struct rw *, struct command *, nbd_completion_callback, asynch_write_op_t);

#endif
