#include "qio.h"
#include <assert.h>
#include <stdio.h>

unsigned char data_at(int64_t offset)
{
  return offset + (offset >> 2) + 'a' + (offset % 7);
}

void fill_testdata(int64_t start, int64_t len, unsigned char* data)
{
  int64_t k;
  for( k = 0; k < len; k++ ) {
    data[k] = data_at(start+k);
  }
}

void check_channel(char threadsafe, qio_chtype_t type, int64_t start, int64_t len, int64_t chunksz, qio_hint_t file_hints, qio_hint_t ch_hints)
{
  qio_file_t* f;
  qio_channel_t* writing;
  qio_channel_t* reading;
  int64_t offset;
  int64_t usesz;
  int64_t end = start + len;
  int err;
  unsigned char* chunk;
  unsigned char* got_chunk;
  int64_t k;
  ssize_t amt_written;
  ssize_t amt_read;
  char* chhints;
  char* fhints;
  FILE* writefp = NULL;
  FILE* readfp = NULL;
  int memory;

  ch_hints = (ch_hints & ~ QIO_CHTYPEMASK) | type;

  memory = 0;

  if( (file_hints & QIO_METHODMASK) == QIO_METHOD_MEMORY ||
      (ch_hints & QIO_METHODMASK) == QIO_METHOD_MEMORY ) {
    memory = 1;
  }
  if( memory ) {
    file_hints = (file_hints & ~ QIO_METHODMASK ) | QIO_METHOD_MEMORY;
    ch_hints = (ch_hints & ~ QIO_METHODMASK ) | QIO_METHOD_MEMORY;
  }
  if( memory && type == QIO_CH_ALWAYS_UNBUFFERED ) return;

  fhints = qio_hints_to_string(file_hints);
  chhints = qio_hints_to_string(ch_hints);
  printf("check_channel(threadsafe=%i, start=%lli, len=%lli, chunksz=%lli, file_hints=%s, ch_hints=%s)\n",
         (int) threadsafe,
         (long long int) start,
         (long long int) len,
         (long long int) chunksz,
         fhints,
         chhints);
  free(fhints);
  free(chhints);

  chunk = malloc(chunksz);
  got_chunk = malloc(chunksz);

  assert(chunk);
  assert(got_chunk);

  if( memory ) {
    err = qio_file_open_mem_ext(&f, NULL, QIO_FDFLAG_READABLE|QIO_FDFLAG_WRITEABLE|QIO_FDFLAG_SEEKABLE, file_hints, NULL);
    assert(!err);
  } else {
    // Open a temporary file.
    err = qio_file_open_tmp(&f, file_hints, NULL);
    assert(!err);

    // Rewind the file
    {
      off_t off;

      err = sys_lseek(f->fd, start, SEEK_SET, &off);
      assert(!err);
    }
  }

  // Create a "write to file" channel.
  err = qio_channel_create(&writing, f, ch_hints, 0, 1, start, end, NULL);
  assert(!err);

  // Write stuff to the file.
  for( offset = start; offset < end; offset += usesz ) {
    usesz = chunksz;
    if( offset + usesz > end ) usesz = end - offset;
    // Fill chunk.
    fill_testdata(offset, usesz, chunk);
    // Write chunk.
    if( writefp ) {
      amt_written = fwrite(chunk, 1, usesz, writefp);
    } else {
      err = qio_channel_write(threadsafe, writing, chunk, usesz, &amt_written);
      assert(!err);
    }
    assert(amt_written == usesz);

  }

  // Attempt to write 1 more byte; we should get EEOF
  // if we've restricted the range of the channel.
  // Write chunk.
  if( writefp ) {
    int got;
    got = fflush(writefp);
    assert(got == 0);
    amt_written = fwrite(chunk, 1, 1, writefp);
    // fwrite might buffer on its own.
    if( amt_written != 0 ) {
      got = fflush(writefp);
      assert(got == EOF);
    }
    assert(errno == EEOF);
  } else {
    err = qio_channel_write(threadsafe, writing, chunk, 1, &amt_written);
    assert(amt_written == 0);
    assert( err == EEOF );
  }

  qio_channel_release(writing);

  // Check that the file is the right length.
  if( !memory ) {
    struct stat stats;
    err = sys_fstat(f->fd, &stats);
    assert(!err);
    assert(stats.st_size == end);
  }

  // That was fun. Now start at the beginning of the file
  // and read the data.
  
  // Rewind the file 
  if( !memory ) {
    off_t off;

    sys_lseek(f->fd, start, SEEK_SET, &off);
    assert(!err);
  }

  // Read the data.
  //err = qio_channel_init_file(&reading, type, f, ch_hints, 1, 0, start, end);
  err = qio_channel_create(&reading, f, ch_hints, 1, 0, start, end, NULL);
  assert(!err);

  // Read stuff from the file.
  for( offset = start; offset < end; offset += usesz ) {
    usesz = chunksz;
    if( offset + usesz > end ) usesz = end - offset;
    // Fill chunk.
    fill_testdata(offset, usesz, chunk);
    memset(got_chunk, 0xff, usesz);
    // Read chunk.
    if( readfp ) {
      amt_read = fread(got_chunk, 1, usesz, readfp);
    } else {
      err = qio_channel_read(threadsafe, reading, got_chunk, usesz, &amt_read);
      assert( err == EEOF || err == 0);
    }
    assert(amt_read == usesz);

    // Compare chunk.
    for( k = 0; k < usesz; k++ ) {
      assert(got_chunk[k] == chunk[k]);
    }
  }

  if( readfp ) {
    amt_read = fread(got_chunk, 1, 1, readfp);
    assert( amt_read == 0 );
    assert( feof(readfp) );
  } else {
    err = qio_channel_read(threadsafe, reading, got_chunk, 1, &amt_read);
    assert( err = EEOF );
  }

  qio_channel_release(reading);
  //err = qio_channel_destroy(&reading);

  // Close the file.
  qio_file_release(f);

  free(chunk);
  free(got_chunk);
}

void check_channels(void)
{
  int s, i, k;
  int64_t starts[] = {0, 7};
  int nstarts = sizeof(starts)/sizeof(int64_t);
  int64_t lens[] = {1, 2, 3, qbytes_iobuf_size + 13, 4 * qbytes_iobuf_size};
  int nlens = sizeof(lens)/sizeof(int64_t);
  int64_t chunkszs[] = {1, 7, 16, qbytes_iobuf_size + 13, 2 * qbytes_iobuf_size};
  int nchunkszs = sizeof(chunkszs)/sizeof(int64_t);
  qio_chtype_t type;
  char threadsafe;
  qio_hint_t hints[] = {QIO_METHOD_DEFAULT, QIO_METHOD_READWRITE, QIO_METHOD_PREADPWRITE, QIO_METHOD_FREADFWRITE, QIO_METHOD_MEMORY, QIO_METHOD_MMAP, QIO_METHOD_PREADPWRITE | QIO_HINT_NOFAST};
  int nhints = sizeof(hints)/sizeof(qio_hint_t);
  int file_hint, ch_hint;

  check_channel(0, QIO_CH_BUFFERED, 0, 1, 1, 0, QIO_METHOD_MEMORY);
  //check_channel(0, QIO_CH_UNBUFFERED, 0, 1, 1, 1, QIO_METHOD_READWRITE | QIO_HINT_NOFAST );
  //check_channel(0, QIO_CH_BUFFERED, 0, 1, 1, 0, QIO_METHOD_MMAP, 1);
  check_channel(0, QIO_CH_BUFFERED, 0, 1, 1, 0, 0 );
  check_channel(0, QIO_CH_BUFFERED, 0, 2, 1, 0, 0 );
  //check_channel(0, 2, 0, 2, 1, 1, QIO_METHOD_READWRITE );
  //check_channel(0, 2, 0, 128*1024, 128*1024, 1);
  //check_channel(0, QIO_CH_UNBUFFERED, 0, 2, 7, QIO_METHOD_READWRITE, QIO_METHOD_READWRITE );
  //check_channel(0, QIO_CH_BUFFERED, 0, 3, 1, QIO_METHOD_DEFAULT, QIO_METHOD_DEFAULT );
  //check_channel(0, QIO_CH_BUFFERED, 7, 1, 1, QIO_METHOD_DEFAULT, QIO_METHOD_MMAP );
  //check_channel(0, QIO_CH_UNBUFFERED, 0, 1, 1, QIO_METHOD_MMAP, QIO_METHOD_DEFAULT );
//check_channel(threadsafe=0, type=buffered, start=7, len=1, chunksz=1, file_hints=default, ch_hints=mmap)
//check_channel(threadsafe=0, type=buffered, start=0, len=1, chunksz=1, file_hints=default, ch_hints=mmap)


  for( file_hint = 0; file_hint < nhints; file_hint++ ) {
    for( ch_hint = 0; ch_hint < nhints; ch_hint++ ) {
      for( i = 0; i < nlens; i++ ) {
        for( s = 0; s < nstarts; s++ ) {
          for( k = 0; k < nchunkszs; k++ ) {
            for( type = 1; type <= QIO_CH_MAX_TYPE; type++ ) {
              for( threadsafe = 0; threadsafe < 2; threadsafe++ ) {
                check_channel(threadsafe, type, starts[s], lens[i], chunkszs[k], hints[file_hint], hints[ch_hint]);
              }
            }
          }
        }
      }
    }
  }

  return;

  for( file_hint = 0; file_hint < nhints; file_hint++ ) {
    printf("Checking very large channel with hints %x\n", hints[file_hint]);
    // Check a very large file.
    check_channel(0, QIO_CH_BUFFERED, 0, 5L*1024L*1024L*1024L, 1024*1024, hints[file_hint], hints[file_hint]);
  }
}

// Check some path functions.
void check_paths(void)
{
  const char* tmp = NULL;
  qio_relative_path(&tmp, "/", "/tmp/foo");
  assert(0==strcmp(tmp, "tmp/foo"));
  qio_free((void*)tmp);
  qio_relative_path(&tmp, "", "/tmp/foo");
  assert(0==strcmp(tmp, "tmp/foo"));
  qio_free((void*)tmp);
  qio_relative_path(&tmp, "/bar/bre/", "/bar/bre/tmp/foo");
  assert(0==strcmp(tmp, "tmp/foo"));
  qio_free((void*)tmp);
  qio_relative_path(&tmp, "/bar/bre", "/bar/bre/tmp/foo");
  assert(0==strcmp(tmp, "tmp/foo"));
  qio_free((void*)tmp);
  qio_relative_path(&tmp, "/a/b/c/d/e", "/a/b/x/y");
  assert(0==strcmp(tmp, "../../../x/y"));
  qio_free((void*)tmp);
  qio_relative_path(&tmp, "/a/b/c/d/e/", "/a/b/x/y");
  assert(0==strcmp(tmp, "../../../x/y"));
  qio_free((void*)tmp);
  qio_relative_path(&tmp, "/a/b/xc/d/e/", "/a/b/x/y");
  assert(0==strcmp(tmp, "../../../x/y"));
  qio_free((void*)tmp);
  qio_relative_path(&tmp, "/a/b/c/d/e/", "/a/b/cx/y");
  assert(0==strcmp(tmp, "../../../cx/y"));
  qio_free((void*)tmp);
  qio_relative_path(&tmp, "/home/mppf/chapel/svn_ferguson/test/types/file", "/home/mppf/chapel/svn_ferguson/test/types/file/freadNoInt.txt");
  assert(0==strcmp(tmp, "freadNoInt.txt"));
  qio_free((void*)tmp);
  qio_relative_path(&tmp, "/home/mppf/chapel/svn_ferguson/test/types/file/", "/home/mppf/chapel/svn_ferguson/test/types/file/freadNoInt.txt");
  assert(0==strcmp(tmp, "freadNoInt.txt"));
  qio_free((void*)tmp);



}


int main(int argc, char** argv)
{

  // use smaller mmap chunks for testing.
  qio_mmap_chunk_iobufs = 1;

  // use smaller qbytes_iobuf_size for testing
  qbytes_iobuf_size = 4*1024;

  check_paths();

  check_channels();


  return 0;
}

