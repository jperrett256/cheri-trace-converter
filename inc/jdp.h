#ifndef JDP_INCLUDE
#define JDP_INCLUDE

#include <inttypes.h>
#include <stdbool.h>
#include <assert.h>
#include "stb_sprintf.h"

#if defined(__gnu_linux__)
 #define JDP_OS_LINUX 1
#elif defined(_WIN32)
 #define JDP_OS_WINDOWS 1
#else
 #error Platform unsupported.
#endif

typedef int8_t      i8;
typedef int16_t     i16;
typedef int32_t     i32;
typedef int64_t     i64;

typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;

typedef i8          b8;
typedef i16         b16;
typedef i32         b32;

typedef float       f32;
typedef double      f64;

#define KILOBYTES(x)    ((x)*1024LL)
#define MEGABYTES(x)    (KILOBYTES(x)*1024LL)
#define GIGABYTES(x)    (MEGABYTES(x)*1024LL)
#define TERABYTES(x)    (GIGABYTES(x)*1024LL)

#define array_count(a)      (sizeof(a) / sizeof((a)[0]))
#define align_pow_2(x, b)   (((x) + ((b) - 1)) & (~((b) - 1)))

#define ARENA_COMMIT_SIZE       KILOBYTES(64)
#define COMMON_TEMP_BUF_LEN    	KILOBYTES(8)

#if JDP_OS_WINDOWS
 #include <windows.h>
#elif JDP_OS_LINUX
 #include <sys/mman.h>
#endif


#if JDP_OS_WINDOWS
char * get_temp_buffer();

#define dbg_print(format, ...)                                                          \
    do {                                                                                \
        stbsp_snprintf(get_temp_buffer(), COMMON_TEMP_BUF_LEN, format, __VA_ARGS__);    \
        OutputDebugStringA(get_temp_buffer());                                          \
    } while (0)

#endif


typedef struct string_t string_t;
struct string_t
{
    char * start;
    i64 size;
};

#define string_lit(x) 	(string_t) { .start = (char *) (x), .size = sizeof(x) - 1 }
#define string_varg(s) 	(int)(s).size, (s).start

string_t string_from_cstr(char * cstr);
string_t string_slice(string_t s, i64 start, i64 end);
string_t string_prefix(string_t s, i64 end);
string_t string_suffix(string_t s, i64 start);
bool string_match(string_t a, string_t b);
bool string_match_prefix(string_t s, string_t prefix);


typedef struct arena_t arena_t;
struct arena_t
{
    void * start;
    u64 pos;
    u64 committed;
    u64 reserved;
};

arena_t arena_alloc(u64 size);
void arena_free(arena_t * arena);
void * arena_push(arena_t * arena, u64 amount);
#define arena_push_array(arena, type, count) ((type *) arena_push(arena, sizeof(type) * (count)))


#ifdef JDP_IMPLEMENTATION

char * get_temp_buffer()
{
    static char buffer[COMMON_TEMP_BUF_LEN];

    return buffer;
}


string_t string_from_cstr(char * cstr)
{
    i64 size = 0;
    while (cstr[size] != '\0') size++;

    string_t result = { cstr, size };
    return result;
}

string_t string_slice(string_t s, i64 start, i64 end)
{
    assert(end >= start);
    assert(s.size >= start);
    assert(s.size >= end);
    return (string_t) { s.start + start, end - start };
}

string_t string_prefix(string_t s, i64 end)
{
    return string_slice(s, 0, end);
}

string_t string_suffix(string_t s, i64 start)
{
    return string_slice(s, start, s.size);
}

bool string_match(string_t a, string_t b)
{
    if (a.size != b.size) return false;

    bool match = true;
    for (i64 i = 0; i < a.size; i++)
    {
        if (a.start[i] != b.start[i])
        {
            match = false;
            break;
        }
    }

    return match;
}

bool string_match_prefix(string_t s, string_t prefix)
{
    if (s.size < prefix.size) return false;
    return string_match(string_prefix(s, prefix.size), prefix);
}


# if JDP_OS_WINDOWS
static void * jdp_mem_reserve(u64 size)
{
    return VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_READWRITE);
}

static bool jdp_mem_commit(void * ptr, u64 size)
{
    return (VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE) != 0);
}

static void jdp_mem_release(void * ptr, u64 size)
{
    (void) size;
    VirtualFree(ptr, 0, MEM_RELEASE)

}
# elif JDP_OS_LINUX
static void * jdp_mem_reserve(u64 size)
{
    void * result = mmap(0, size, PROT_NONE, MAP_PRIVATE |  MAP_ANONYMOUS, -1, 0);
    if (result == (void *) -1) result = 0;
    return result;
}

static bool jdp_mem_commit(void * ptr, u64 size)
{
    return (mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0);
}

static void jdp_mem_release(void * ptr, u64 size)
{
    munmap(ptr, size);
}
# endif

arena_t arena_alloc(u64 size)
{
    void * start = jdp_mem_reserve(size);

    arena_t arena = {0};
    arena.start = start;
    arena.pos = 0;
    arena.committed = 0;
    arena.reserved = size;

    return arena;
}

void arena_free(arena_t * arena)
{
    jdp_mem_release(arena->start, arena->reserved);

    arena->start = NULL;
}

void * arena_push(arena_t * arena, u64 amount)
{
    u64 old_pos = arena->pos;
    assert(old_pos == align_pow_2(old_pos, 8));
    u64 new_pos = align_pow_2(old_pos + amount, 8);
    assert(new_pos >= old_pos);

    if (new_pos > arena->reserved)
    {
        assert(!"arena ran out of reserved memory");
    }

    while (new_pos > arena->committed)
    {
        bool success = jdp_mem_commit((u8 *) arena->start + arena->committed, ARENA_COMMIT_SIZE);
        assert(success);
        arena->committed += ARENA_COMMIT_SIZE;
    }

    arena->pos = new_pos;

    return (u8 *) arena->start + old_pos;
}

#endif /* JDP_IMPLEMENTATION */
#endif /* JDP_INCLUDE */
