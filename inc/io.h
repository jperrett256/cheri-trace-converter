#ifndef IO_INCLUDE
#define IO_INCLUDE

#include "jdp.h"

#include <stdio.h>
#include <zlib.h>
#include <lz4frame.h>

enum trace_reader_type_t
{
    TRACE_READER_TYPE_UNCOMPRESSED_OR_GZIP,
    TRACE_READER_TYPE_LZ4
};

typedef struct lz4_reader_t lz4_reader_t;
struct lz4_reader_t
{
    FILE * file;
    LZ4F_dctx * ctx;
    u8 * src_buf;
    u8 * dst_buf;
    u8 * src_current;
    u8 * dst_current;
    size_t src_remaining;
    size_t dst_remaining;
    bool src_eof;
    bool finished_frame;
};

typedef struct trace_reader_t trace_reader_t;
struct trace_reader_t
{
    u8 type;
    union
    {
        gzFile gzip;
        lz4_reader_t lz4;
    } as;
};


enum trace_writer_type_t
{
    TRACE_WRITER_TYPE_UNCOMPRESSED,
    TRACE_WRITER_TYPE_GZIP,
    TRACE_WRITER_TYPE_LZ4
};

typedef struct lz4_writer_t lz4_writer_t;
struct lz4_writer_t
{
    FILE * file;
    LZ4F_cctx * ctx;
    size_t src_size;
    u8 * src_buf;
    u8 * dst_buf;
    size_t dst_capacity;
};

typedef struct trace_writer_t trace_writer_t;
struct trace_writer_t
{
    u8 type;
    union
    {
        FILE * uncompressed;
        gzFile gzip;
        lz4_writer_t lz4;
    } as;
};

trace_reader_t trace_reader_open(arena_t * arena, char * filename, u8 type);
bool trace_reader_get(trace_reader_t * reader, void * entry, size_t entry_size);
void trace_reader_close(trace_reader_t * reader);

trace_writer_t trace_writer_open(arena_t * arena, char * filename, u8 type);
void trace_writer_emit(trace_writer_t * writer, const void * entry, size_t entry_size);
void trace_writer_close(trace_writer_t * writer);

u8 guess_reader_type(char * filename);
u8 guess_writer_type(char * filename);

#endif /* IO_INCLUDE */
