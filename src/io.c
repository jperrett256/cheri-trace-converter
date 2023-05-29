#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "io.h"
#include "utils.h"

#include <stdlib.h>

#define LZ4_BUFFER_SIZE MEGABYTES(1)
static_assert(LZ4_BUFFER_SIZE >= LZ4F_HEADER_SIZE_MAX, "Inappropriate LZ4 buffer size.");


static void memmove_down(void * dst, const void * src, i64 size)
{
    assert(size >= 0);
    assert(dst <= src);

    u8 * dst_ptr = (u8 *) dst;
    const u8 * src_ptr = (const u8 *) src;
    for (i64 i = size - 1; i >= 0; i--)
    {
        dst_ptr[i] = src_ptr[i];
    }
}

// static size_t get_block_size(const LZ4F_frameInfo_t * info)
// {
//     switch (info->blockSizeID) {
//         case LZ4F_default:
//         case LZ4F_max64KB:  return KILOBYTES(64);
//         case LZ4F_max256KB: return KILOBYTES(256);
//         case LZ4F_max1MB:   return MEGABYTES(1);
//         case LZ4F_max4MB:   return MEGABYTES(4);
//         default: assert(false);
//     }
// }

static void lz4_reader_open(arena_t * arena, int fd, lz4_reader_t * state)
{
    // TODO create own arena instead?

    state->file = fdopen(fd, "rb");
    assert(state->file);

    LZ4F_errorCode_t lz4_error = LZ4F_createDecompressionContext(&state->ctx, LZ4F_VERSION);
    assert(!LZ4F_isError(lz4_error));

    state->src_buf = arena_push_array(arena, u8, LZ4_BUFFER_SIZE);
    // TODO test just using current_entry as the destination buffer
    state->dst_buf = arena_push_array(arena, u8, LZ4_BUFFER_SIZE);
    state->src_current = state->src_buf;
    state->dst_current = state->dst_buf;
    state->src_remaining = 0;
    state->dst_remaining = 0;

    // {
    //     assert(state->src_current == state->src_buf);
    //     state->src_remaining = fread(state->src_buf, 1, LZ4_BUFFER_SIZE, state->file);

    //     LZ4F_frameInfo_t info;
    //     size_t consumed_size = LZ4_BUFFER_SIZE;
    //     size_t lz4_res = LZ4F_getFrameInfo(state->ctx, &info, state->src_buf, &consumed_size);
    //     assert(!LZ4F_isError(lz4_res));
    //     state->src_remaining -= consumed_size;
    //     state->src_current += consumed_size;

    //     size_t block_size = get_block_size(&info);

    //     printf("block size: %lu\n", block_size);
    // }
}

static void lz4_reader_close(lz4_reader_t * state)
{
    assert(state->ctx);
    assert(state->file);

    LZ4F_errorCode_t lz4_error = LZ4F_freeDecompressionContext(state->ctx);
    assert(!LZ4F_isError(lz4_error));
    state->ctx = NULL;

    fclose(state->file);
    state->file = NULL;
}

static bool lz4_reader_get_entry(lz4_reader_t * state, const void * entry, size_t entry_size)
{
    while (state->dst_remaining < entry_size)
    {
        // check if we need to read more source
        if (state->src_remaining == 0 && !state->src_eof)
        {
            state->src_remaining = fread(state->src_buf, 1, LZ4_BUFFER_SIZE, state->file);
            assert(!ferror(state->file));
            if (state->src_remaining == 0)
                state->src_eof = true;
            state->src_current = state->src_buf;
        }

        // check if we are done
        if (state->src_eof && state->finished_frame)
        {
            assert(state->dst_remaining == 0);
            return false;
        }

        // move data to make space at end of destination buffer
        if (state->dst_current != state->dst_buf)
        {
            memmove_down(state->dst_buf, state->dst_current, state->dst_remaining);
            state->dst_current = state->dst_buf;
        }

        size_t src_size = state->src_remaining;
        size_t dst_size = LZ4_BUFFER_SIZE - state->dst_remaining;

        assert(state->dst_current == state->dst_buf);
        size_t lz4_ret = LZ4F_decompress(state->ctx,
            &state->dst_buf[state->dst_remaining], &dst_size, state->src_current, &src_size, NULL);
        assert(!LZ4F_isError(lz4_ret));

        state->src_remaining -= src_size;
        state->src_current += src_size;
        state->dst_remaining += dst_size;

        state->finished_frame = (lz4_ret == 0);
    }

    assert(state->dst_remaining >= entry_size);
    assert(state->dst_current < state->dst_buf + LZ4_BUFFER_SIZE);

    assert(entry_size <= INT64_MAX);
    u8 * entry_ptr = (u8 *) entry;
    for (i64 i = 0; i < (i64) entry_size; i++)
    {
        entry_ptr[i] = state->dst_current[i];
    }

    state->dst_current += entry_size;
    state->dst_remaining -= entry_size;
    return true;
}


static bool gz_at_eof(int bytes_read, int expected_bytes)
{
    if (bytes_read < 0)
    {
        printf("ERROR: error reading gzip file.\n");
        // TODO call gzerror?
        quit();
    }

    if (bytes_read < expected_bytes)
    {
        assert(bytes_read >= 0);
        if (bytes_read != 0)
        {
            printf("ERROR: attempted to read %d bytes, was only able to read %d bytes.\n", expected_bytes, bytes_read);
        }
        return true;
    }

    return false;
}


static i32 num_readers_open = 0;
static bool will_check_readers_closed = false;
static void check_readers_closed(void)
{
    if (num_readers_open != 0)
    {
        printf("ERROR: found %d readers still open.\n", num_readers_open);
        fflush(stdout);
        assert(false);
        quit();
    }
}

trace_reader_t trace_reader_open(arena_t * arena, char * filename, u8 type)
{
    assert(num_readers_open < INT32_MAX);
    num_readers_open++;

    if (!will_check_readers_closed)
    {
        will_check_readers_closed = true;
        atexit(check_readers_closed);
    }

    trace_reader_t reader = {0};
    reader.type = type;

    int fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Could not open file for reading: \"%s\".\n", filename);
        quit();
    }

    {
        int success;

        struct stat buf;
        success = fstat(fd, &buf);
        assert(success != -1);

        if (S_ISFIFO(buf.st_mode))
        {
            int pipe_size = fcntl(fd, F_GETPIPE_SZ);
            assert(pipe_size != -1);

            int desired_pipe_size = MEGABYTES(1);

            fprintf(stderr, "Reading from Unix FIFO with buffer size %d, will resize buffer to %d.\n",
                pipe_size, desired_pipe_size);

            success = fcntl(fd, F_SETPIPE_SZ, desired_pipe_size);
            assert(success != -1);
        }
    }

    switch (type)
    {
        case TRACE_READER_TYPE_UNCOMPRESSED_OR_GZIP:
        {
            reader.as.gzip = gzdopen(fd, "rb");
            assert(reader.as.gzip);
            // TODO tune buffer size with gzbuffer? manual buffering?
        } break;
        case TRACE_READER_TYPE_LZ4:
        {
            lz4_reader_open(arena, fd, &reader.as.lz4);
        } break;
        default: assert(!"Impossible");
    }

    return reader;
}

bool trace_reader_get(trace_reader_t * reader, void * entry, size_t entry_size)
{
    switch (reader->type)
    {
        case TRACE_READER_TYPE_UNCOMPRESSED_OR_GZIP:
        {
            assert(reader->as.gzip);
            int bytes_read = gzread(reader->as.gzip, entry, entry_size);

            assert(entry_size <= INT_MAX);
            if (gz_at_eof(bytes_read, entry_size)) return false;

            return true;
        } break;
        case TRACE_READER_TYPE_LZ4:
        {
            return lz4_reader_get_entry(&reader->as.lz4, entry, entry_size);
        } break;
        default: assert(!"Impossible");
    }

    assert(!"Impossible");
    return false;
}

void trace_reader_close(trace_reader_t * reader)
{
    switch (reader->type)
    {
        case TRACE_READER_TYPE_UNCOMPRESSED_OR_GZIP:
        {
            assert(reader->as.gzip);
            gzclose(reader->as.gzip);
            reader->as.gzip = NULL;
        } break;
        case TRACE_READER_TYPE_LZ4:
        {
            lz4_reader_close(&reader->as.lz4);
        } break;
        default: assert(!"Impossible");
    }

    assert(num_readers_open > 0);
    num_readers_open--;
}


void lz4_writer_open(arena_t * arena, int fd, lz4_writer_t * state)
{
    state->file = fdopen(fd, "wb");
    assert(state->file);

    LZ4F_errorCode_t lz4_error = LZ4F_createCompressionContext(&state->ctx, LZ4F_VERSION);
    assert(!LZ4F_isError(lz4_error));

    LZ4F_preferences_t lz4_prefs = // TODO
    {
        {
            LZ4F_max4MB,
            LZ4F_blockLinked, // NOTE this affects ability to randomly access traces
            LZ4F_noContentChecksum,
            LZ4F_frame,
            0, /* content size unknown */
            0, /* no dictID provided */
            LZ4F_noBlockChecksum
        },
        0, /* default compression level */
        0, /* disable "always flush" */
        0, /* do not favor decompression speed over compression ratio */
        { 0, 0, 0 } /* reserved */
    };

    state->src_size = 0;
    state->src_buf = arena_push_array(arena, u8, LZ4_BUFFER_SIZE);

    state->dst_capacity = LZ4F_compressBound(LZ4_BUFFER_SIZE, &lz4_prefs);
    state->dst_buf = arena_push_array(arena, u8, state->dst_capacity);

    size_t header_size = LZ4F_compressBegin(state->ctx, state->dst_buf, state->dst_capacity, &lz4_prefs);
    assert(!LZ4F_isError(header_size));

    size_t bytes_written = fwrite(state->dst_buf, 1, header_size, state->file);
    assert(bytes_written == header_size);
}

static void lz4_writer_compress(lz4_writer_t * state)
{
    assert(state->src_size > 0);

    size_t compressed_size = LZ4F_compressUpdate(state->ctx,
        state->dst_buf, state->dst_capacity, state->src_buf, state->src_size, NULL);
    assert(!LZ4F_isError(compressed_size));

    size_t bytes_written = fwrite(state->dst_buf, 1, compressed_size, state->file);
    assert(bytes_written == compressed_size);

    state->src_size = 0;
}

void lz4_writer_emit_entry(lz4_writer_t * state, const void * entry, size_t entry_size)
{
    assert(state->file);
    assert(entry_size <= LZ4_BUFFER_SIZE);

    // TODO what happens if you just call compressUpdate with the entry (no src buffer)?
    if (state->src_size + entry_size > LZ4_BUFFER_SIZE)
        lz4_writer_compress(state);

    assert(entry_size <= INT64_MAX);
    const u8 * entry_ptr = (const u8 *) entry;
    for (i64 i = 0; i < (i64) entry_size; i++)
    {
        assert(state->src_size + i < LZ4_BUFFER_SIZE);
        state->src_buf[state->src_size + i] = entry_ptr[i];
    }
    state->src_size += entry_size;
}

// NOTE must be careful to close these lz4 writers in particular
void lz4_writer_close(lz4_writer_t * state)
{
    assert(state->file);

    if (state->src_size > 0)
        lz4_writer_compress(state);

    {
        size_t compressed_size = LZ4F_compressEnd(state->ctx, state->dst_buf, state->dst_capacity, NULL);
        assert(!LZ4F_isError(compressed_size));

        size_t bytes_written = fwrite(state->dst_buf, 1, compressed_size, state->file);
        assert(bytes_written == compressed_size);
    }

    assert(state->ctx);
    LZ4F_errorCode_t lz4_error = LZ4F_freeCompressionContext(state->ctx);
    assert(!LZ4F_isError(lz4_error));
    state->ctx = NULL;

    fclose(state->file);
    state->file = NULL;
}


static i32 num_writers_open = 0;
static bool will_check_writers_closed = false;
static void check_writers_closed(void)
{
    if (num_writers_open != 0)
    {
        printf("ERROR: found %d writers still open.\n", num_writers_open);
        fflush(stdout);
        assert(false);
        quit();
    }
}

trace_writer_t trace_writer_open(arena_t * arena, char * filename, u8 type)
{
    assert(num_writers_open < INT32_MAX);
    num_writers_open++;

    if (!will_check_writers_closed)
    {
        will_check_writers_closed = true;
        atexit(check_writers_closed);
    }

    trace_writer_t writer = {0};
    writer.type = type;

    int fd = open(filename, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
    if (fd == -1)
    {
        fprintf(stderr, "Could not open file for writing: \"%s\".\n", filename);
        quit();
    }

    {
        int success;

        struct stat buf;
        success = fstat(fd, &buf);
        assert(success != -1);

        if (S_ISFIFO(buf.st_mode))
        {
            int pipe_size = fcntl(fd, F_GETPIPE_SZ);
            assert(pipe_size != -1);

            int desired_pipe_size = MEGABYTES(1);

            fprintf(stderr, "Writing to Unix FIFO with buffer size %d, will resize buffer to %d.\n",
                pipe_size, desired_pipe_size);

            success = fcntl(fd, F_SETPIPE_SZ, desired_pipe_size);
            assert(success != -1);
        }
    }

    switch (type)
    {
        case TRACE_WRITER_TYPE_UNCOMPRESSED:
        {
            writer.as.uncompressed = fdopen(fd, "wb");
            assert(writer.as.uncompressed);
        } break;
        case TRACE_WRITER_TYPE_GZIP:
        {
            writer.as.gzip = gzdopen(fd, "wb");
            assert(writer.as.gzip);
            // TODO tune buffer size with gzbuffer? manual buffering?
        } break;
        case TRACE_WRITER_TYPE_LZ4:
        {
            lz4_writer_open(arena, fd, &writer.as.lz4);
        } break;
        default: assert(!"Impossible");
    }

    return writer;
}

void trace_writer_emit(trace_writer_t * writer, const void * entry, size_t entry_size)
{
    switch (writer->type)
    {
        case TRACE_WRITER_TYPE_UNCOMPRESSED:
        {
            assert(writer->as.uncompressed);
            size_t bytes_written = fwrite(entry, 1, entry_size, writer->as.uncompressed);
            assert(bytes_written == entry_size);
        } break;
        case TRACE_WRITER_TYPE_GZIP:
        {
            assert(writer->as.gzip);
            int bytes_written = gzwrite(writer->as.gzip, entry, entry_size);
            assert(bytes_written == entry_size);
        } break;
        case TRACE_WRITER_TYPE_LZ4:
        {
            lz4_writer_emit_entry(&writer->as.lz4, entry, entry_size);
        } break;
        default: assert(!"Impossible");
    }
}

void trace_writer_close(trace_writer_t * writer)
{
    switch (writer->type)
    {
        case TRACE_WRITER_TYPE_UNCOMPRESSED:
        {
            assert(writer->as.uncompressed);
            fclose(writer->as.uncompressed);
            writer->as.uncompressed = NULL;
        } break;
        case TRACE_WRITER_TYPE_GZIP:
        {
            assert(writer->as.gzip);
            gzclose(writer->as.gzip);
            writer->as.gzip = NULL;
        } break;
        case TRACE_WRITER_TYPE_LZ4:
        {
            lz4_writer_close(&writer->as.lz4);
        } break;
        default: assert(!"Impossible");
    }

    assert(num_writers_open > 0);
    num_writers_open--;
}


static string_t get_file_extension(char * path)
{
    string_t path_str = string_from_cstr(path);

    for (i64 i = path_str.size - 1; i >= 0; i--)
    {
        if (path_str.ptr[i] == '.')
        {
            i64 start_idx = i + 1;
            return (string_t) { &path_str.ptr[start_idx], path_str.size - start_idx };
        }

        if (path_str.ptr[i] == '/' || path_str.ptr[i] == '\\') break;
    }

    return (string_t) { NULL, 0 };
}

u8 guess_reader_type(char * filename)
{
    string_t extension = get_file_extension(filename);

    if (string_match(extension, string_lit("gz")))
    {
        return TRACE_READER_TYPE_UNCOMPRESSED_OR_GZIP;
    }

    if (string_match(extension, string_lit("lz4")))
    {
        return TRACE_READER_TYPE_LZ4;
    }

    fprintf(stderr,
        "WARNING: Unrecognised extension on \"%s\", assuming uncompressed or gzip compressed (for reading).\n", filename);
    return TRACE_READER_TYPE_UNCOMPRESSED_OR_GZIP;
}

u8 guess_writer_type(char * filename)
{
    string_t extension = get_file_extension(filename);

    if (string_match(extension, string_lit("gz")))
    {
        return TRACE_WRITER_TYPE_GZIP;
    }

    if (string_match(extension, string_lit("lz4")))
    {
        return TRACE_WRITER_TYPE_LZ4;
    }

    fprintf(stderr,
        "WARNING: Unrecognised extension on \"%s\", assuming uncompressed (for writing).\n", filename);
    return TRACE_WRITER_TYPE_UNCOMPRESSED;
}


