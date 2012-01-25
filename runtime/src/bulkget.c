#include "chplrt.h"
#include "chpl-comm-compiler-macros.h"
#include "chpl-comm.h"

#include "bulkget.h"

qbytes_t* bulk_get_bytes(int32_t src_locale, qbytes_t* src_addr)
{
  qbytes_t* ret;
  int64_t src_len;
  err_t err;

  // First, get the length of the bytes.
  CHPL_COMM_GET( src_len, src_locale, & src_addr->len, int64_t, CHPL_TYPE_int64_t, 1, -1, "<internal>");
//  chpl_comm_get

  err = qbytes_create_calloc(&ret, src_len);
  if( err ) return NULL;


  // TODO -- note -- technically, this should be gasnet_get_bulk,
  // since we don't want to require src/dst to have a particular alignment.

  // Next, get the data itself.
  CHPL_COMM_GET( *(uint8_t*)ret->data, src_locale, & src_addr->data, uint8_t, CHPL_TYPE_uint8_t, src_len, -1, "<internal>");

  // Great! All done.
  return ret; 
}

/*
void bulk_get_style(int32_t src_locale, qio_style_t* dst_addr, qio_style_t* src_addr)
{
  CHPL_COMM_GET( *(uint8_t*)dst_addr, src_locale, src_addr, uint8_t, CHPL_TYPE_uint8_t, sizeof(qio_style_t), -1, "<internal>");
}
*/

err_t bulk_put_buffer(int32_t dst_locale, void* dst_addr, int64_t dst_len,
                      qbuffer_t* buf, qbuffer_iter_t start, qbuffer_iter_t end)
{
  int64_t num_bytes = qbuffer_iter_num_bytes(start, end);
  ssize_t num_parts = qbuffer_iter_num_parts(start, end);
  struct iovec* iov = NULL;
  size_t iovcnt;
  size_t i,j;
  int iov_onstack;
  err_t err;
 
  if( num_bytes < 0 || num_parts < 0 || start.offset < buf->offset_start || end.offset > buf->offset_end ) return EINVAL;

  MAYBE_STACK_ALLOC(num_parts*sizeof(struct iovec), iov, iov_onstack);
  if( ! iov ) return ENOMEM;

  err = qbuffer_to_iov(buf, start, end, num_parts, iov, NULL, &iovcnt);
  if( err ) goto error;

  j = 0;
  for( i = 0; i < iovcnt; i++ ) {
    if( j + iov[i].iov_len > dst_len ) goto error_nospace;
    //memcpy(PTR_ADDBYTES(ptr, j), iov[i].iov_base, iov[i].iov_len);

    // TODO -- note -- technically, this should be gasnet_put_bulk,
    // since we don't want to require src/dst to have a particular alignment.
    CHPL_COMM_PUT( (*(uint8_t*)iov[i].iov_base), // macro gets ptr to this
                    dst_locale,
                    PTR_ADDBYTES(dst_addr, j),
                    uint8_t, CHPL_TYPE_uint8_t,
                    iov[i].iov_len,
                    -1, "<internal>" );

    j += iov[i].iov_len;
  }

  MAYBE_STACK_FREE(iov, iov_onstack);
  return 0;

error_nospace:
  err = EMSGSIZE;
error:
  MAYBE_STACK_FREE(iov, iov_onstack);
  return err;
}

         
