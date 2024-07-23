/* nbd client library in userspace: state machine
 * Copyright Red Hat
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <assert.h>

#include "internal.h"

/* Common code for parsing a reply to NBD_OPT_*. */
static int
prepare_for_reply_payload (struct nbd_handle *h, uint32_t opt)
{
  const size_t maxpayload = sizeof h->sbuf.or.payload;
  uint64_t magic;
  uint32_t option;
  uint32_t reply;
  uint32_t len;

  magic = be64toh (h->sbuf.or.option_reply.magic);
  option = be32toh (h->sbuf.or.option_reply.option);
  reply = be32toh (h->sbuf.or.option_reply.reply);
  len = be32toh (h->sbuf.or.option_reply.replylen);
  if (magic != NBD_REP_MAGIC || option != opt) {
    set_error (0, "handshake: invalid option reply magic or option");
    return -1;
  }

  /* Validate lengths that the state machine depends on. */
  switch (reply) {
  case NBD_REP_ACK:
    if (len != 0) {
      set_error (0, "handshake: invalid NBD_REP_ACK option reply length");
      return -1;
    }
    break;
  case NBD_REP_INFO:
    /* Can't enforce an upper bound, thanks to unknown INFOs */
    if (len < sizeof h->sbuf.or.payload.export.info) {
      set_error (0, "handshake: NBD_REP_INFO reply length too small");
      return -1;
    }
    break;
  case NBD_REP_META_CONTEXT:
    if (len <= sizeof h->sbuf.or.payload.context.context ||
        len > sizeof h->sbuf.or.payload.context) {
      set_error (0, "handshake: invalid NBD_REP_META_CONTEXT reply length");
      return -1;
    }
    break;
  }

  /* Read the following payload if it is short enough to fit in the
   * static buffer.  If it's too long, skip it.
   */
  len = be32toh (h->sbuf.or.option_reply.replylen);
  if (len > MAX_REQUEST_SIZE) {
    set_error (0, "handshake: invalid option reply length");
    return -1;
  }
  else if (len <= maxpayload)
    h->rbuf = &h->sbuf.or.payload;
  else
    h->rbuf = NULL;
  h->rlen = len;
  return 0;
}

/* Check an unexpected server reply error.
 *
 * This calls set_error with a descriptive error message and returns
 * 0.  Unless there is a further unexpected error while processing
 * this error, in which case it calls set_error and returns -1.
 */
static int
handle_reply_error (struct nbd_handle *h)
{
  uint32_t len;
  uint32_t reply;
  char *msg = NULL;

  len = be32toh (h->sbuf.or.option_reply.replylen);
  reply = be32toh (h->sbuf.or.option_reply.reply);
  if (!NBD_REP_IS_ERR (reply)) {
    set_error (0, "handshake: unexpected option reply type %d", reply);
    return -1;
  }

  assert (NBD_MAX_STRING < sizeof h->sbuf.or.payload);
  if (len > NBD_MAX_STRING) {
    set_error (0, "handshake: option error string too long");
    return -1;
  }

  /* Decode expected errors into a nicer string.
   *
   * XXX Note this string comes directly from the server, and most
   * libnbd users simply print the error using 'fprintf'.  We really
   * ought to quote this string somehow, but we don't have a useful
   * function for that.
   */
  if (len > 0) {
    if (asprintf (&msg, ": %.*s",
                  (int)len, h->sbuf.or.payload.err_msg) == -1) {
      set_error (errno, "asprintf");
      return -1;
    }
  }

  switch (reply) {
  case NBD_REP_ERR_UNSUP:
    set_error (ENOTSUP, "the operation is not supported by the server%s",
               msg ? : "");
      break;
    case NBD_REP_ERR_POLICY:
      set_error (0, "server policy prevents the operation%s",
                 msg ? : "");
      break;
    case NBD_REP_ERR_PLATFORM:
      set_error (0, "the operation is not supported by the server platform%s",
                 msg ? : "");
      break;
    case NBD_REP_ERR_INVALID:
      set_error (EINVAL, "the server rejected this operation as invalid%s",
                 msg ? : "");
      break;
    case NBD_REP_ERR_TOO_BIG:
      set_error (EINVAL, "the operation is too large to process%s",
                 msg ? : "");
      break;
    case NBD_REP_ERR_TLS_REQD:
      set_error (ENOTSUP, "the server requires TLS encryption first%s",
                 msg ? : "");
      break;
    case NBD_REP_ERR_UNKNOWN:
      set_error (ENOENT, "the server has no export named '%s'%s",
                 h->export_name, msg ? : "");
      break;
    case NBD_REP_ERR_SHUTDOWN:
      set_error (ESHUTDOWN, "the server is shutting down%s",
                 msg ? : "");
      break;
    case NBD_REP_ERR_BLOCK_SIZE_REQD:
      set_error (EINVAL, "the server requires specific block sizes%s",
                 msg ? : "");
      break;
    default:
      set_error (0, "handshake: unknown reply from the server: 0x%" PRIx32 "%s",
                 reply, msg ? : "");
    }
  free (msg);

  return 0;
}

/* State machine for parsing the fixed newstyle handshake. */

STATE_MACHINE {
 NEWSTYLE.START:
  if (h->opt_mode) {
    /* NEWSTYLE can be entered multiple times, from MAGIC.CHECK_MAGIC
     * (h->opt_current is 0, run through OPT_STRUCTURED_REPLY for
     * opt_mode, or OPT_GO otherwise) and during various nbd_opt_*
     * calls during NEGOTIATING (h->opt_current is set, run just the
     * states needed).  Each previous state has informed us what we
     * still need to do.
     */
    switch (h->opt_current) {
    case NBD_OPT_GO:
    case NBD_OPT_INFO:
      if ((h->gflags & LIBNBD_HANDSHAKE_FLAG_FIXED_NEWSTYLE) == 0)
        SET_NEXT_STATE (%OPT_EXPORT_NAME.START);
      else
        SET_NEXT_STATE (%OPT_META_CONTEXT.START);
      return 0;
    case NBD_OPT_LIST:
      SET_NEXT_STATE (%OPT_LIST.START);
      return 0;
    case NBD_OPT_ABORT:
      if ((h->gflags & LIBNBD_HANDSHAKE_FLAG_FIXED_NEWSTYLE) == 0) {
        SET_NEXT_STATE (%.DEAD);
        set_error (ENOTSUP, "handshake: server is not using fixed newstyle");
        return 0;
      }
      SET_NEXT_STATE (%PREPARE_OPT_ABORT);
      return 0;
    case NBD_OPT_LIST_META_CONTEXT:
    case NBD_OPT_SET_META_CONTEXT:
      SET_NEXT_STATE (%OPT_META_CONTEXT.START);
      return 0;
    case NBD_OPT_STRUCTURED_REPLY:
      SET_NEXT_STATE (%OPT_STRUCTURED_REPLY.START);
      return 0;
    case NBD_OPT_EXTENDED_HEADERS:
      SET_NEXT_STATE (%OPT_EXTENDED_HEADERS.START);
      return 0;
    case NBD_OPT_STARTTLS:
      SET_NEXT_STATE (%OPT_STARTTLS.START);
      return 0;
    case 0:
      break;
    default:
      abort ();
    }
  }

  assert (h->opt_current == 0);
  h->rbuf = &h->sbuf;
  h->rlen = sizeof h->sbuf.gflags;
  SET_NEXT_STATE (%RECV_GFLAGS);
  return 0;

 NEWSTYLE.RECV_GFLAGS:
  switch (recv_into_rbuf (h)) {
  case -1: SET_NEXT_STATE (%.DEAD); return 0;
  case 0:  SET_NEXT_STATE (%CHECK_GFLAGS);
  }
  return 0;

 NEWSTYLE.CHECK_GFLAGS:
  uint32_t cflags;

  h->gflags &= be16toh (h->sbuf.gflags);
  if ((h->gflags & LIBNBD_HANDSHAKE_FLAG_FIXED_NEWSTYLE) == 0 &&
      h->tls == LIBNBD_TLS_REQUIRE) {
    SET_NEXT_STATE (%.DEAD);
    set_error (ENOTSUP, "handshake: server is not using fixed newstyle, "
               "but handle TLS setting is 'require' (2)");
    return 0;
  }

  if ((h->gflags & LIBNBD_HANDSHAKE_FLAG_FIXED_NEWSTYLE) == 0)
    h->protocol = "newstyle";
  else
    h->protocol = "newstyle-fixed";

  cflags = h->gflags;
  h->sbuf.cflags = htobe32 (cflags);
  h->wbuf = &h->sbuf;
  h->wlen = 4;
  SET_NEXT_STATE (%SEND_CFLAGS);
  return 0;

 NEWSTYLE.SEND_CFLAGS:
  switch (send_from_wbuf (h)) {
  case -1: SET_NEXT_STATE (%.DEAD); return 0;
  case 0:
    /* Start sending options. */
    if ((h->gflags & LIBNBD_HANDSHAKE_FLAG_FIXED_NEWSTYLE) == 0) {
      if (h->opt_mode)
        SET_NEXT_STATE (%.NEGOTIATING);
      else
        SET_NEXT_STATE (%OPT_EXPORT_NAME.START);
    }
    else
      SET_NEXT_STATE (%OPT_STARTTLS.START);
  }
  return 0;

 NEWSTYLE.PREPARE_OPT_ABORT:
  assert ((h->gflags & LIBNBD_HANDSHAKE_FLAG_FIXED_NEWSTYLE) != 0);
  h->sbuf.option.version = htobe64 (NBD_NEW_VERSION);
  h->sbuf.option.option = htobe32 (NBD_OPT_ABORT);
  h->sbuf.option.optlen = htobe32 (0);
  h->chunks_sent++;
  h->wbuf = &h->sbuf;
  h->wlen = sizeof h->sbuf.option;
  SET_NEXT_STATE (%SEND_OPT_ABORT);
  return 0;

 NEWSTYLE.SEND_OPT_ABORT:
  switch (send_from_wbuf (h)) {
  case -1: SET_NEXT_STATE (%.DEAD); return 0;
  case 0:
    SET_NEXT_STATE (%SEND_OPTION_SHUTDOWN);
  }
  return 0;

 NEWSTYLE.SEND_OPTION_SHUTDOWN:
  /* We don't care if the server replies to NBD_OPT_ABORT.  However,
   * unless we are in opt mode, we want to preserve the error message
   * from a failed OPT_GO by moving to DEAD instead.
   */
  if (h->sock->ops->shut_writes (h, h->sock)) {
    if (h->opt_mode)
      SET_NEXT_STATE (%.CLOSED);
    else
      SET_NEXT_STATE (%.DEAD);
  }
  return 0;

 NEWSTYLE.FINISHED:
  SET_NEXT_STATE (%.READY);
  return 0;

} /* END STATE MACHINE */
