module BufferInternals {
  use SysBasic;
  use Error;

  /*
     This way does not work because we lose calls to chpl__initCopy.

  _extern class qbytes_ptr_t {
    var data:c_ptr;
    var len:int(64);
    var ref_cnt:c_long;
    // free_function:qbytes_free_t
    var flags:uint(8);
    // unused1, unused2, unused3
  }

  _extern class qbuffer_ptr_t {
    var ref_cnt:c_long;
    // deque:deque_t
    var offset_start:int(64);
    var offset_end:int(64);
  }
  _extern record qbuffer_iter_t {
    var offset:int(64);
    // iter:deque_iterator_t
  }
  */

  _extern type qbytes_ptr_t;
  _extern type qbuffer_ptr_t;
  _extern type qbuffer_iter_t;
  _extern const QBYTES_PTR_NULL:qbytes_ptr_t;
  _extern const QBUFFER_PTR_NULL:qbuffer_ptr_t;


  _extern proc qbytes_retain(qb:qbytes_ptr_t);
  _extern proc qbytes_release(qb:qbytes_ptr_t):err_t;
  _extern proc qbytes_len(qb:qbytes_ptr_t):int(64);
  _extern proc qbytes_data(qb:qbytes_ptr_t):c_ptr;

  _extern proc qbytes_create_iobuf(inout ret:qbytes_ptr_t):err_t;
  _extern proc qbytes_create_calloc(inout ret:qbytes_ptr_t, len:int(64)):err_t;

  _extern proc qbuffer_iter_null():qbuffer_iter_t;

  /*
  _extern proc qbuffer_init(inout buf:qbuffer_t):err_t;
  _extern proc qbuffer_destroy(inout buf:qbuffer_t);
  */
  _extern proc qbuffer_create(inout buf:qbuffer_ptr_t):err_t;
  _extern proc qbuffer_retain(buf:qbuffer_ptr_t);
  _extern proc qbuffer_release(buf:qbuffer_ptr_t):err_t;

  _extern proc qbuffer_append(buf:qbuffer_ptr_t, bytes:qbytes_ptr_t, skip_bytes:int(64), len_bytes:int(64)):err_t;
  _extern proc qbuffer_append_buffer(buf:qbuffer_ptr_t, src:qbuffer_ptr_t, src_start:qbuffer_iter_t, src_end:qbuffer_iter_t):err_t;
  _extern proc qbuffer_prepend(buf:qbuffer_ptr_t, bytes:qbytes_ptr_t, skip_bytes:int(64), len_bytes:int(64)):err_t;
  _extern proc qbuffer_flatten(buf:qbuffer_ptr_t, start:qbuffer_iter_t, end:qbuffer_iter_t, inout bytes_out):err_t;
  //_extern proc qbuffer_clone(buf:qbuffer_ptr_t, start:qbuffer_iter_t, end:qbuffer_iter_t, inout buf_out:qbuffer_t):err_t;
  _extern proc qbuffer_copyout(buf:qbuffer_ptr_t, start:qbuffer_iter_t, end:qbuffer_iter_t, inout x, size):err_t;
  _extern proc qbuffer_copyin(buf:qbuffer_ptr_t, start:qbuffer_iter_t, end:qbuffer_iter_t, inout x, size):err_t;

  _extern proc qbuffer_begin(buf:qbuffer_ptr_t):qbuffer_iter_t;
  _extern proc qbuffer_end(buf:qbuffer_ptr_t):qbuffer_iter_t;
  _extern proc qbuffer_iter_next_part(buf:qbuffer_ptr_t, inout it:qbuffer_iter_t);
  _extern proc qbuffer_iter_prev_part(buf:qbuffer_ptr_t, inout it:qbuffer_iter_t);
  _extern proc qbuffer_iter_advance(buf:qbuffer_ptr_t, inout it:qbuffer_iter_t, amt:int(64));

  _extern proc qbuffer_iter_get(it: qbuffer_iter_t, end:qbuffer_iter_t, 
                                inout bytes_out:qbytes_ptr_t,
                                inout skip_out:int(64),
                                inout len_out:int(64));
  _extern proc qbuffer_iter_num_bytes(start:qbuffer_iter_t, end:qbuffer_iter_t):int(64);


  _extern proc debug_print_qbuffer_iter(inout it:qbuffer_iter_t);


  _extern proc qbuffer_start_offset(buf:qbuffer_ptr_t):int(64);
  _extern proc qbuffer_end_offset(buf:qbuffer_ptr_t):int(64);
  _extern proc qbuffer_reposition(buf:qbuffer_ptr_t, new_offset_start:int(64));

  _extern proc bulk_get_bytes(src_locale:int, src_addr:qbytes_ptr_t):qbytes_ptr_t;

  _extern proc bulk_put_buffer(dst_locale:int, dst_addr:c_ptr, dst_len:int(64), buf:qbuffer_ptr_t, start:qbuffer_iter_t, end:qbuffer_iter_t):err_t;

}

module Buffers {
  use BufferInternals; // TODO -- just use . for the symbols.

  // Now define the Chapel types using the originals..
  record bytes {
    var home:locale;
    var _bytes_internal:qbytes_ptr_t;
  }

  record buffer_iterator {
    var home:locale;
    var _bufit_internal:qbuffer_iter_t;
  }
  record buffer_range {
    var start:buffer_iterator;
    var end:buffer_iterator;
  }

  proc buffer_range.len:int(64) {
    var ret:int(64);
    on this.start.home {
      ret = qbuffer_iter_num_bytes(this.start._bufit_internal,
                                   this.end._bufit_internal);
    }
    return ret;
  }

  // FUTURE -- we could create const buffers
  // with a boolean type param
  record buffer {
    var home:locale;
    var _buf_internal:qbuffer_ptr_t;
  }

  // bytes methods.
  proc bytes.bytes() {
    this._internal = c_nil;
    this.home = here;
  }
  proc bytes.bytes(len:int(64)) {
    seterr(nil, qbytes_create_calloc(this._bytes_internal, len));
    this.home = here;
  }

  proc create_iobuf():bytes {
    var ret:bytes;
    seterr(nil, qbytes_create_iobuf(ret._bytes_internal));
    ret.home = here;
    return ret;
  }

  // TODO -- shouldn't have to write this this way!
  proc chpl__initCopy(x: bytes) {
    if x.home == here {
      qbytes_retain(x._bytes_internal);
      return x;
    } else {
      var ret:bytes;
      ret.home = here;
      writeln("Bulk moving bytes");
      ret._bytes_internal = bulk_get_bytes(x.home.uid, x._bytes_internal); 
      return ret;
    }
  }

  proc =(ret:bytes, x:bytes) {
    // retain -- release
    if( x.home == ret.home ) {
      on x.home {
        qio_bytes_retain(x._bytes_internal);
      }

      on ret.home {
        qio_bytes_release(ret._bytes_internal);
      }

      ret.home = x.home;
      ret._bytes_internal = x._bytes_internal;
    } else {
      on ret.home {
        qio_bytes_release(ret._bytes_internal);
      }
      ret.home = here;
      writeln("Bulk moving bytes");
      ret._bytes_internal = bulk_get_bytes(x.home.uid, x._bytes_internal); 
    }
    return ret;
  }

  proc bytes.~bytes() {
    on this.home {
      qbytes_release(this._bytes_internal);
      this._bytes_internal = QBYTES_PTR_NULL;
    }
  }
  proc bytes.len:int(64) {
    var ret:int(64);
    on this.home {
     ret = qbytes_len(this._bytes_internal);
    }
    return ret;
  }

  // buffer iterator
  proc buffer_iterator.buffer_iterator() {
    this.home = here;
    this._bufit_internal = qbuffer_iter_null();
  }
  /* don't believe this is necessary...
  proc chpl__initCopy(x: buffer_iterator) {
    return x;
  }
  */

  // buffer methods.
  proc buffer.buffer() {
    this.home = here;
    seterr(nil, qbuffer_create(this._buf_internal));
  }

  // buffer.flatten is special in that it creates a 
  // bytes object wherever it is called (and communicates it 
  // intelligently). It is use in buffer's initCopy.
  proc buffer.flatten(range:buffer_range, err:ErrorHandler = nil):bytes {
    var ret:bytes;
    if this.home == here {
      seterr(err,qbuffer_flatten(this._buf_internal, range.start._bufit_internal, range.end._bufit_internal, ret));
    } else {
      var dst_locale = here;
      var dst_len:int(64) = range.len;
      ret = new bytes(dst_len);
      var dst_addr = qbytes_data(ret._bytes_internal);
      on this.home {
        // Copy the buffer to the bytes...
        seterr(err, bulk_put_buffer(dst_locale.uid, dst_addr, dst_len,
                                    this._buf_internal,
                                    range.start._bufit_internal,
                                    range.end._bufit_internal) );
      }
    }
    return ret;
  }

  // TODO -- shouldn't have to write this this way!
  proc chpl__initCopy(x: buffer) {
    if x.home == here {
      qbuffer_retain(x._buf_internal);
      return x;
    } else {
      var ret:buffer;
      var start_offset:int(64);
      var end_offset:int(64);
     
      on x.home {
        start_offset = qbuffer_start_offset(x._buf_internal);
        end_offset = qbuffer_end_offset(x._buf_internal);
      }

      var b = new bytes(end_offset - start_offset);
      var len = qbytes_len(b._bytes_internal);
      var ptr = qbytes_data(b._bytes_internal);

      // Now create a local buffer with the right data
      // in it starting at the right position.
      qbuffer_reposition(ret._buf_internal, start_offset);

      var there = here;

      on x.home {
        seterr(nil, bulk_put_buffer(there.uid, ptr, len, x._buf_internal,
                                    qbuffer_begin(x._buf_internal),
                                    qbuffer_end(x._buf_internal)));
      }

      ret.append(b);
      return ret;
    }
  }

  proc =(ret:buffer, x:buffer) {
    // retain -- release
    if( x.home == ret.home ) {
      on x.home {
        qbuffer_retain(x._buf_internal);
      }

      on ret.home {
        qbuffer_release(ret._buf_internal);
      }

      ret.home = x.home;
      ret._buf_internal = x._buf_internal;
    } else {
      on ret.home {
        qbuffer_release(ret._buf_internal);
      }
      ret.home = here;

      var start_offset:int(64);
      var end_offset:int(64);
     
      on x.home {
        start_offset = qbuffer_start_offset(x._buf_internal);
        end_offset = qbuffer_end_offset(x._buf_internal);
      }

      var b = new bytes(end_offset - start_offset);
      var len = qbytes_len(b._bytes_internal);
      var ptr = qbytes_data(b._bytes_internal);

      // Now create a local buffer with the right data
      // in it starting at the right position.
      qbuffer_reposition(ret._buf_internal, start_offset);

      var there = here;

      on x.home {
        seterr(nil, bulk_put_buffer(there.uid, ptr, len, x._buf_internal,
                                    qbuffer_begin(x._buf_internal),
                                    qbuffer_end(x._buf_internal)));
      }

      ret.append(b);
    }
  }


  proc buffer.~buffer() {
    on this.home {
      qbuffer_release(this._buf_internal);
      this._buf_internal = QBUFFER_PTR_NULL;
    }
  }

  proc buffer.len {
    var ret:int(64);
    on this.home {
      ret = qbuffer_len(this._buf_internal);
    }
    return ret;
  }

  proc buffer.append(b:bytes, skip_bytes:int(64) = 0, len_bytes:int(64) = b.len, err:ErrorHandler = nil) {
    on this.home {
      seterr(err, qbuffer_append(this._buf_internal, b._bytes_internal, skip_bytes, len_bytes));
    }
  }

  proc buffer.append(buf:buffer, part:buffer_range = buf.all(), err:ErrorHandler = nil) {
    on this.home {
      seterr(err, qbuffer_append_buffer(this._buf_internal, buf._buf_internal, part.start._bufit_internal, part.end._bufit_internal));
    }
  }

  proc buffer.prepend(b:bytes, skip_bytes:int(64) = 0, len_bytes:int(64) = b.len, err:ErrorHandler = nil) {
    on this.home {
      seterr(err, qbuffer_prepend(this._buf_internal, b._bytes_internal, skip_bytes, len_bytes));
    }
  }
  proc buffer.start():buffer_iterator {
    var ret:buffer_iterator;
    ret.home = this.home;
    on this.home {
      ret._bufit_internal = qbuffer_begin(this._buf_internal);
    }
    return ret;
  }
  proc buffer.end():buffer_iterator {
    var ret:buffer_iterator;
    ret.home = this.home;
    on this.home {
      ret._bufit_internal = qbuffer_end(this._buf_internal);
    }
    return ret;
  }
  proc buffer.all():buffer_range {
    return new buffer_range(this.start(), this.end());
  }

  proc buffer.next_part(inout it:buffer_iterator) {
    on this.home {
      var x = it;
      qbuffer_iter_next_part(this._buf_internal, x._bufit_internal);
      it = x;
    }
  }
  proc buffer.prev_part(inout it:buffer_iterator) {
    on this.home {
      var x = it;
      qbuffer_iter_prev_part(this._buf_internal, x._bufit_internal);
      it = x;
    }
  }
  proc buffer.advance(inout it:buffer_iterator, amount:int(64)) {
    on this.home {
      var x = it;
      qbuffer_iter_advance(this._buf_internal, x._bufit_internal, amount);
      it = x;
    }
  }

  proc buffer_iterator.debug_print()
  {
    on this.home {
      debug_print_qbuffer_iter(this._bufit_internal);
    }
  }

  /*
  proc buffer.clone(range:buffer_range, err:ErrorHandler = nil):buffer {
    on this.home {
      var ret:buffer;
      seterr(qbuffer_clone(this._buf_internal, range.start, range.end, ret._buf_internal));
      return ret;
    }
  }*/

  // methods to read/write basic types.
  proc buffer.copyout(it:buffer_iterator, out value, err:ErrorHandler = nil):buffer_iterator {
    var ret:buffer_iterator;
    ret.home = this.home;
    on this.home {
      var end:buffer_iterator = it;
      var tmp:value.type;
      var sz = numBytes(value.type);
      this.advance(end, sz);
      seterr(err, qbuffer_copyout(this._buf_internal, it._bufit_internal, end._bufit_internal, tmp, sz));
      value = tmp;
      ret = end;
    }
    return ret;
  }

  //proc buffer.copyin(inout it:buffer_iterator, value, inout err:err_t) {
  proc buffer.copyin( it:buffer_iterator, value, err:ErrorHandler = nil):buffer_iterator {
    var ret:buffer_iterator;
    ret.home = this.home;
    on this.home {
      //writeln("iterator on way in");
      //debug_print_qbuffer_iter(it._bufit_internal);
      var end = it;
      var tmp = value;
      var sz = numBytes(value.type);
      //writeln("iterator copied to end");
      //debug_print_qbuffer_iter(end._bufit_internal);

      //writeln("sz is ", sz);
      this.advance(end, sz);
      //debug_print_qbuffer_iter(end._bufit_internal);
      seterr(err, qbuffer_copyin(this._buf_internal, it._bufit_internal, end._bufit_internal, tmp, sz));
      ret = end;
    }
    return ret;
  }
}
