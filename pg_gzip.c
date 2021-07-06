/***********************************************************************
 *
 * Project:  PgSQL gzip/gunzip
 * Purpose:  Main file.
 *
 ***********************************************************************
 * Copyright 2019 Paul Ramsey <pramsey@cleverelephant.ca>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 ***********************************************************************/

/* System */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PostgreSQL */
#include <postgres.h>
#include <fmgr.h>
#include <funcapi.h>

/* Libdeflate */
#include <libdeflate.h>

extern void _PG_init(void);

/* Set up PgSQL */
PG_MODULE_MAGIC;

void _PG_init(void) {
  libdeflate_set_memory_allocator(palloc, pfree);
}


/**
* gzip an uncompressed bytea
*/
Datum pg_gzip(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(pg_gzip);
Datum pg_gzip(PG_FUNCTION_ARGS)
{
  bytea* uncompressed = PG_GETARG_BYTEA_P(0);
  int32 compression_level = PG_GETARG_INT32(1);
  uint8* in = (uint8*)(VARDATA(uncompressed));
  size_t in_size = VARSIZE_ANY_EXHDR(uncompressed);

  struct libdeflate_compressor *enc = libdeflate_alloc_compressor(compression_level);
  if (enc == NULL)
    elog(ERROR, "could not initialize libdeflate");

  size_t bound = libdeflate_gzip_compress_bound(enc, in_size);

  uint8* out = (uint8*)palloc(bound);
  size_t bound_real = libdeflate_gzip_compress(enc, in, in_size, out, bound);

  bytea *compressed = palloc(bound_real + VARHDRSZ);
  SET_VARSIZE(compressed, bound_real + VARHDRSZ);
  memcpy(VARDATA(compressed), out, bound_real);
  pfree(out);
  libdeflate_free_compressor(enc);
  PG_FREE_IF_COPY(uncompressed, 0);
  PG_RETURN_POINTER(compressed);
}


Datum pg_gunzip(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(pg_gunzip);
Datum pg_gunzip(PG_FUNCTION_ARGS)
{
  struct libdeflate_decompressor *dec = libdeflate_alloc_decompressor();
  if (dec == NULL)
    elog(ERROR, "could not initialize libdeflate");

  bytea* compressed = PG_GETARG_BYTEA_P(0);
  uint8* in = (uint8*)(VARDATA(compressed));
  size_t in_size = VARSIZE_ANY_EXHDR(compressed);

  // Start from something (2:1 compression ratio for basic binary data), and then double the buffer size until we can decompress it properly.
  size_t bound = (in_size * 2) < 65536 ? in_size : 65536;

  elog(LOG, "Computed bound (in: %ld), %ld", in_size, bound);

  bytea *uncompressed = NULL;
  do {
    size_t actual_size = 0;
    uint8* out = palloc(bound);

    enum libdeflate_result result = libdeflate_gzip_decompress(dec, in, in_size, out, bound, &actual_size);

    if (result == LIBDEFLATE_INSUFFICIENT_SPACE) {
      if (bound * 2 <= bound) {
        elog(ERROR, "Could not deflate. Data corrupt or too large to handle.");
        break;
      }
      bound *= 2;
      pfree(out);
      out = NULL;
      continue;
    }

    if (result == LIBDEFLATE_SUCCESS) {
      /* Construct output bytea */
      uncompressed = palloc(actual_size + VARHDRSZ);
      SET_VARSIZE(uncompressed, actual_size + VARHDRSZ);
      memcpy(VARDATA(uncompressed), out, actual_size);
      pfree(out);
      libdeflate_free_decompressor(dec);
      break;
    } else {
      elog(ERROR, "Could not deflate, result %d", result);
      break;
    }
  } while (bound != 0);

  elog(LOG, "Done & done.");
  PG_FREE_IF_COPY(compressed, 0);
  PG_RETURN_POINTER(uncompressed);
}
