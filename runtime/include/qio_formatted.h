#ifndef _QIO_FORMATTED_H_
#define _QIO_FORMATTED_H_

#include "qio.h"

#include "bswap.h"

// true 1 false 0   __bool_true_false_are_defined
#include <stdbool.h>
#include <ctype.h>
#include <stdlib.h>
#include <inttypes.h>
#include <math.h> // HUGE_VAL
#include <float.h> // DBL_MANT_DIG
#include <wchar.h>
#include <assert.h>


#include "qio_style.h"

extern int qio_glocale_utf8; // for testing use.
#define QIO_GLOCALE_UTF8 1
#define QIO_GLOCALE_ASCII -2
#define QIO_GLOCALE_OTHER -1

void qio_set_glocale(void);

// Read/Write methods for Binary I/O

static always_inline
err_t qio_channel_read_int8(const int threadsafe, qio_channel_t* restrict ch, int8_t* restrict ptr) {
  return qio_channel_read_amt(threadsafe, ch, ptr, 1);
}

static always_inline
err_t qio_channel_write_int8(const int threadsafe, qio_channel_t* restrict ch, int8_t x) {
  return qio_channel_write_amt(threadsafe, ch, &x, 1);
}

static always_inline
err_t qio_channel_read_uint8(const int threadsafe, qio_channel_t* restrict ch, uint8_t* restrict ptr) {
  return qio_channel_read_amt(threadsafe, ch, ptr, 1);
}

static always_inline
err_t qio_channel_write_uint8(const int threadsafe, qio_channel_t* restrict ch, uint8_t x) {
  return qio_channel_write_amt(threadsafe, ch, &x, 1);
}



static always_inline
err_t qio_channel_read_int16(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, int16_t* restrict ptr) {
  err_t err;
  int16_t x;
  err = qio_channel_read_amt(threadsafe, ch, &x, 2);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x = be16toh(x);
  if( byteorder == QIO_LITTLE ) x = le16toh(x);
  *ptr = x;
  return 0;
}

static always_inline
err_t qio_channel_write_int16(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, int16_t x) {
  if( byteorder == QIO_BIG ) x = htobe16(x);
  if( byteorder == QIO_LITTLE ) x = htole16(x);
  return qio_channel_write_amt(threadsafe, ch, &x, 2);
}

static always_inline
err_t qio_channel_read_uint16(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, uint16_t* restrict ptr) {
  err_t err;
  uint16_t x;
  err = qio_channel_read_amt(threadsafe, ch, &x, 2);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x = be16toh(x);
  if( byteorder == QIO_LITTLE ) x = le16toh(x);
  *ptr = x;
  return 0;
}

static always_inline
err_t qio_channel_write_uint16(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, uint16_t x) {
  if( byteorder == QIO_BIG ) x = htobe16(x);
  if( byteorder == QIO_LITTLE ) x = htole16(x);
  return qio_channel_write_amt(threadsafe, ch, &x, 2);
}


static always_inline
err_t qio_channel_read_int32(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, int32_t* restrict ptr) {
  err_t err;
  int32_t x;
  err = qio_channel_read_amt(threadsafe, ch, &x, 4);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x = be32toh(x);
  if( byteorder == QIO_LITTLE ) x = le32toh(x);
  *ptr = x;
  return 0;
}

static always_inline
err_t qio_channel_write_int32(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, int32_t x) {
  if( byteorder == QIO_BIG ) x = htobe32(x);
  if( byteorder == QIO_LITTLE ) x = htole32(x);
  return qio_channel_write_amt(threadsafe, ch, &x, 4);
}

static always_inline
err_t qio_channel_read_uint32(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, uint32_t* restrict ptr) {
  err_t err;
  uint32_t x;
  err = qio_channel_read_amt(threadsafe, ch, &x, 4);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x = be32toh(x);
  if( byteorder == QIO_LITTLE ) x = le32toh(x);
  *ptr = x;
  return 0;
}

static always_inline
err_t qio_channel_write_uint32(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, uint32_t x) {
  if( byteorder == QIO_BIG ) x = htobe32(x);
  if( byteorder == QIO_LITTLE ) x = htole32(x);
  return qio_channel_write_amt(threadsafe, ch, &x, 4);
}

static always_inline
err_t qio_channel_read_int64(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, int64_t* restrict ptr) {
  int64_t x;
  err_t err;
  err = qio_channel_read_amt(threadsafe, ch, &x, 8);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x = be64toh(x);
  if( byteorder == QIO_LITTLE ) x = le64toh(x);
  *ptr = x;
  return 0;
}

static always_inline
err_t qio_channel_write_int64(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, int64_t x) {
  if( byteorder == QIO_BIG ) x = htobe64(x);
  if( byteorder == QIO_LITTLE ) x = htole64(x);
  return qio_channel_write_amt(threadsafe, ch, &x, 8);
}

static always_inline
err_t qio_channel_read_uint64(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, uint64_t* restrict ptr) {
  uint64_t x;
  err_t err;
  err = qio_channel_read_amt(threadsafe, ch, &x, 8);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x = be64toh(x);
  if( byteorder == QIO_LITTLE ) x = le64toh(x);
  *ptr = x;
  return 0;
}

static always_inline
err_t qio_channel_write_uint64(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, uint64_t x) {
  if( byteorder == QIO_BIG ) x = htobe64(x);
  if( byteorder == QIO_LITTLE ) x = htole64(x);
  return qio_channel_write_amt(threadsafe, ch, &x, 8);
}

// Reading/writing varints in the same format as Google Protocol Buffers.
err_t qio_channel_read_uvarint(const int threadsafe, qio_channel_t* restrict ch, uint64_t* restrict ptr);
err_t qio_channel_read_svarint(const int threadsafe, qio_channel_t* restrict ch, int64_t* restrict ptr);
err_t qio_channel_write_uvarint(const int threadsafe, qio_channel_t* restrict ch, uint64_t num);
err_t qio_channel_write_svarint(const int threadsafe, qio_channel_t* restrict ch, int64_t num);


static always_inline
err_t qio_channel_read_int(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, void* restrict ptr, size_t len, int issigned) {

  ssize_t signed_len = len;
  if( issigned ) signed_len = - signed_len;

  switch ( signed_len ) {
    case 1:
      return qio_channel_read_uint8(threadsafe, ch, ptr);
    case -1:
      return qio_channel_read_int8(threadsafe, ch, ptr);
    case 2:
      return qio_channel_read_uint16(threadsafe, byteorder, ch, ptr);
    case -2:
      return qio_channel_read_int16(threadsafe, byteorder, ch, ptr);
    case 4:
      return qio_channel_read_uint32(threadsafe, byteorder, ch, ptr);
    case -4:
      return qio_channel_read_int32(threadsafe, byteorder, ch, ptr);
    case 8:
      return qio_channel_read_int64(threadsafe, byteorder, ch, ptr);
    case -8:
      return qio_channel_read_uint64(threadsafe, byteorder, ch, ptr);
    default:
      return EINVAL;
  }
}

static always_inline
err_t qio_channel_write_int(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, const void* restrict ptr, size_t len, int issigned) {

  ssize_t signed_len = len;
  if( issigned ) signed_len = - signed_len;

  switch ( signed_len ) {
    case 1:
      return qio_channel_write_uint8(threadsafe, ch, *(const uint8_t*) ptr);
    case -1:
      return qio_channel_write_int8(threadsafe, ch, *(const int8_t*) ptr);
    case 2:
      return qio_channel_write_uint16(threadsafe, byteorder, ch, *(const uint16_t*) ptr);
    case -2:
      return qio_channel_write_int16(threadsafe, byteorder, ch, *(const int16_t*) ptr);
    case 4:
      return qio_channel_write_uint32(threadsafe, byteorder, ch, *(const uint32_t*) ptr);
    case -4:
      return qio_channel_write_int32(threadsafe, byteorder, ch, *(const int32_t*) ptr);
    case 8:
      return qio_channel_write_uint64(threadsafe, byteorder, ch, *(const uint64_t*) ptr);
    case -8:
      return qio_channel_write_int64(threadsafe, byteorder, ch, *(const int64_t*) ptr);
    default:
      return EINVAL;
  }
}

static always_inline
err_t qio_channel_read_float32(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, float* restrict ptr) {
  union {
   uint32_t i;
   float f;
  } x;
  err_t err;

  err = qio_channel_read_amt(threadsafe, ch, &x, 4);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x.i = be32toh(x.i);
  if( byteorder == QIO_LITTLE ) x.i = le32toh(x.i);
  *ptr = x.f;
  return 0;
}

static always_inline
err_t qio_channel_write_float32(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, float x) {
  union {
   uint32_t i;
   float f;
  } u;

  u.f = x;

  if( byteorder == QIO_BIG ) u.i = htobe32(u.i);
  if( byteorder == QIO_LITTLE ) u.i = htole32(u.i);
  return qio_channel_write_amt(threadsafe, ch, &u, 4);
}


static always_inline
err_t qio_channel_read_float64(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, double* restrict ptr) {
  union {
   uint64_t i;
   double f;
  } x;
  err_t err;

  err = qio_channel_read_amt(threadsafe, ch, &x, 8);
  if( err ) return err;
  if( byteorder == QIO_BIG ) x.i = be64toh(x.i);
  if( byteorder == QIO_LITTLE ) x.i = le64toh(x.i);
  *ptr = x.f;
  return 0;
}


static always_inline
err_t qio_channel_write_float64(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, double x) {
  union {
   uint64_t i;
   double f;
  } u;

  u.f = x;

  if( byteorder == QIO_BIG ) u.i = htobe64(u.i);
  if( byteorder == QIO_LITTLE ) u.i = htole64(u.i);
  return qio_channel_write_amt(threadsafe, ch, &u, 8);
}

static always_inline
err_t qio_channel_read_float(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, void* restrict ptr, size_t len) {
  switch ( len ) {
    case 4:
      return qio_channel_read_float32(threadsafe, byteorder, ch, ptr);
    case 8:
      return qio_channel_read_float64(threadsafe, byteorder, ch, ptr);
    default:
      return EINVAL;
  }
}

static always_inline
err_t qio_channel_write_float(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, const void* restrict ptr, size_t len) {
  switch ( len ) {
    case 4:
      return qio_channel_write_float32(threadsafe, byteorder, ch, *(const float*) ptr);
    case 8:
      return qio_channel_write_float64(threadsafe, byteorder, ch, *(const double*) ptr);
    default:
      return EINVAL;
  }
}

static always_inline
err_t qio_channel_read_complex(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, void* restrict re_ptr, void* restrict im_ptr, size_t len) {
  err_t err;
  switch ( len ) {
    case 4:
      err = qio_channel_read_float32(threadsafe, byteorder, ch, re_ptr);
      if( ! err ) {
        err = qio_channel_read_float32(threadsafe, byteorder, ch, im_ptr);
      }
      break;
    case 8:
      err = qio_channel_read_float64(threadsafe, byteorder, ch, re_ptr);
      if( ! err ) {
        err = qio_channel_read_float64(threadsafe, byteorder, ch, im_ptr);
      }
      break;
    default:
      err = EINVAL;
  }
  return err;
}

static always_inline
err_t qio_channel_write_complex(const int threadsafe, const int byteorder, qio_channel_t* restrict ch, const void* restrict re_ptr, const void* restrict im_ptr, size_t len) {
  err_t err;
  switch ( len ) {
    case 4:
      err = qio_channel_write_float32(threadsafe, byteorder, ch, *(const float*) re_ptr);
      if( ! err ) {
        err = qio_channel_write_float32(threadsafe, byteorder, ch, *(const float*) im_ptr);
      }
      break;
    case 8:
      err = qio_channel_write_float64(threadsafe, byteorder, ch, *(const double*) re_ptr);
      if( ! err ) {
        err = qio_channel_write_float64(threadsafe, byteorder, ch, *(const double*) im_ptr);
      }
      break;
    default:
      err = EINVAL;
  }
  return err;
}

// string binary style:
// -1 -- 1 byte of length before
// -2 -- 2 bytes of length before
// -4 -- 4 bytes of length before
// -8 -- 8 bytes of length before
// -10 -- variable byte length before (hi-bit 1 means more, little endian)
// -0x01XX -- read until terminator XX is read
//  + -- nonzero positive -- read exactly this length.
err_t qio_channel_read_string(const int threadsafe, const int byteorder, const int64_t str_style, qio_channel_t* restrict ch, const char* restrict * restrict out, ssize_t* restrict len_out, ssize_t maxlen);

// string binary style:
// -1 -- 1 byte of length before
// -2 -- 2 bytes of length before
// -4 -- 4 bytes of length before
// -8 -- 8 bytes of length before
// -10 -- variable byte length before (hi-bit 1 means more, little endian)
// -0x01XX -- read until terminator XX is read
//  + -- nonzero positive -- read exactly this length.
err_t qio_channel_write_string(const int threadsafe, const int byteorder, const int64_t str_style, qio_channel_t* restrict ch, const char* restrict ptr, ssize_t len);


err_t qio_channel_scan_bool(const int threadsafe, qio_channel_t* restrict ch, uint8_t* restrict out);
err_t qio_channel_print_bool(const int threadsafe, qio_channel_t* restrict ch, uint8_t num);

err_t qio_channel_scan_int(const int threadsafe, qio_channel_t* restrict ch, void* restrict out, size_t len, int issigned);
err_t qio_channel_scan_float(const int threadsafe, qio_channel_t* restrict ch, void* restrict out, size_t len);
err_t qio_channel_print_int(const int threadsafe, qio_channel_t* restrict ch, const void* restrict ptr, size_t len, int issigned);
err_t qio_channel_print_float(const int threadsafe, qio_channel_t* restrict ch, const void* restrict ptr, size_t len);

err_t qio_channel_scan_complex(const int threadsafe, qio_channel_t* restrict ch, void* restrict re_out, void* restrict im_out, size_t len);
err_t qio_channel_print_complex(const int threadsafe, qio_channel_t* restrict ch, const void* restrict re_ptr, const void* im_ptr, size_t len);

// These methods read or write UTF-8 characters (code points).
//err_t qio_channel_read_char(const int threadsafe, qio_channel_t* restrict ch, int32_t* restrict chr);


/* BEGIN UTF-8 decoder from http://bjoern.hoehrmann.de/utf-8/decoder/dfa */
// Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

static const uint8_t qio_utf8d[] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
  8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
  0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
  0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
  0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
  1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
  1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
  1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
};

static inline
uint32_t qio_utf8_decode(uint32_t* restrict state,
                     uint32_t* restrict codep,
                     uint32_t byte) {
  uint32_t type = qio_utf8d[byte];

  *codep = (*state != UTF8_ACCEPT) ?
    (byte & 0x3fu) | (*codep << 6) :
    (0xff >> type) & (byte);

  *state = qio_utf8d[256 + *state*16 + type];
  return *state;
}
/* END UTF-8 decoder from http://bjoern.hoehrmann.de/utf-8/decoder/dfa */

err_t _qio_channel_read_char_slow_unlocked(qio_channel_t* restrict ch, int32_t* restrict chr);

static inline
err_t qio_channel_read_char(const int threadsafe, qio_channel_t* restrict ch, int32_t* restrict chr) {
  err_t err;
  uint32_t codepoint, state;
  
  if( qio_glocale_utf8 == 0 ) {
    qio_set_glocale();
  }

  if( threadsafe ) {
    err = qio_lock(&ch->lock);
    if( err ) return err;
  }

  err = 0;

  // Fast path: an entire multi-byte sequence
  // is stored in the buffers.
  if( qio_glocale_utf8 > 0 &&
      4 <= VOID_PTR_DIFF(ch->cached_end, ch->cached_cur) ) {
    if( qio_glocale_utf8 == QIO_GLOCALE_UTF8 ) {
      state = 0;
      while( 1 ) {
        qio_utf8_decode(&state,
                        &codepoint,
                        *(unsigned char*)ch->cached_cur);
        ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,1);
        if (state <= 1) {
          break;
        }
      }
      if( state == UTF8_ACCEPT ) {
        *chr = codepoint;
      } else {
        *chr = 0xfffd; // replacement character
        err = EILSEQ;
      }
    } else if( qio_glocale_utf8 == QIO_GLOCALE_ASCII ) {
      // character == byte.
      *chr = *(unsigned char*)ch->cached_cur;
      ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,1);
    }
  } else {
    err = _qio_channel_read_char_slow_unlocked(ch, chr);
  }

 //unlock:
  _qio_channel_set_error_unlocked(ch, err);
  if( threadsafe ) {
    qio_unlock(&ch->lock);
  }

  return err;
}


err_t _qio_channel_write_char_slow_unlocked(qio_channel_t* restrict ch, int32_t chr);

static inline
err_t qio_channel_write_char(const int threadsafe, qio_channel_t* restrict ch, int32_t chr)
{
  err_t err;

  if( qio_glocale_utf8 == 0 ) {
    qio_set_glocale();
  }

  if( threadsafe ) {
    err = qio_lock(&ch->lock);
    if( err ) return err;
  }

  err = 0;

  if( qio_glocale_utf8 > 0 &&
      4 <= VOID_PTR_DIFF(ch->cached_end, ch->cached_cur) ) {
    if( qio_glocale_utf8 == QIO_GLOCALE_UTF8 ) {
      if( chr < 0 ) {
        err = EILSEQ;
      } else if( chr < 0x80 ) {
        // OK, we got a 1-byte character; case #1
        *(unsigned char*)ch->cached_cur = (unsigned char) chr;
        ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,1);
      } else if( chr < 0x800 ) {
        // OK, we got a fits-in-2-bytes character; case #2
        *(unsigned char*)ch->cached_cur = (0xc0 | (chr >> 6));
        *(((unsigned char*)ch->cached_cur)+1) = (0x80 | (chr & 0x3f));
        ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,2);
      } else if( chr < 0x10000 ) {
        // OK, we got a fits-in-3-bytes character; case #3
        *(unsigned char*)ch->cached_cur = (0xe0 | (chr >> 12));
        *(((unsigned char*)ch->cached_cur)+1) = (0x80 | ((chr >> 6) & 0x3f));
        *(((unsigned char*)ch->cached_cur)+2) = (0x80 | (chr & 0x3f));
        ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,3);
      } else {
        // OK, we got a fits-in-4-bytes character; case #4
        *(unsigned char*)ch->cached_cur = (0xf0 | (chr >> 18));
        *(((unsigned char*)ch->cached_cur)+1) = (0x80 | ((chr >> 12) & 0x3f));
        *(((unsigned char*)ch->cached_cur)+2) = (0x80 | ((chr >> 6) & 0x3f));
        *(((unsigned char*)ch->cached_cur)+3) = (0x80 | (chr & 0x3f));
        ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,4);
      }
    } else if( qio_glocale_utf8 == QIO_GLOCALE_ASCII ) {
      *(unsigned char*)ch->cached_cur = (unsigned char) chr;
      ch->cached_cur = VOID_PTR_ADD(ch->cached_cur,1);
    }
  } else {
    err = _qio_channel_write_char_slow_unlocked(ch, chr);
  }

//unlock:
  _qio_channel_set_error_unlocked(ch, err);
  if( threadsafe ) {
    qio_unlock(&ch->lock);
  }

  return err;
}


err_t qio_channel_skip_past_newline(const int threadsafe, qio_channel_t* restrict ch);

err_t qio_channel_write_newline(const int threadsafe, qio_channel_t* restrict ch);

err_t qio_channel_scan_string(const int threadsafe, qio_channel_t* restrict ch, const char* restrict * restrict out, ssize_t* restrict len_out, ssize_t maxlen);

// returns 0 if it matched, or EFORMAT if it did not.
err_t qio_channel_scan_literal(const int threadsafe, qio_channel_t* restrict ch, const char* restrict match, ssize_t len, int skipws);

// Prints a string according to the style.
err_t qio_channel_print_string(const int threadsafe, qio_channel_t* restrict ch, const char* restrict ptr, ssize_t len);

// Prints a string as-is (this really just calls qio_channel_write_amt).
err_t qio_channel_print_literal(const int threadsafe, qio_channel_t* restrict ch, const char* restrict ptr, ssize_t len);

#endif
