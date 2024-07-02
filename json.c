/* json.c - Json reader/writer for C language.
 *
 *
 * (c) 2019 Oleg Alexeev <oleg.alexeev@inbox.ru> (https://github.com/exgit)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include "json.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned short ushort;


// Error reporting macro. Disable or redefine it as needed.
#if 1
#define ERROR(fmt, ...) fprintf(stderr, \
    "ERROR: %s():%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define ERROR(fmt, ...) ((void)0)
#endif


/*****************************************************************************
* Arena memory allocator.
*****************************************************************************/

// magic number for free block header
#define MAGIC 0xFFFF7575

// allocation granularity
#define GRANULARITY (sizeof(void*))

// rounding up to GRANULARITY bytes
#define ROUNDUP(x) x += (-x & (GRANULARITY - 1))

// arena chunk header
typedef struct _marena_chunk_hdr_t marena_chunk_hdr_t;
struct _marena_chunk_hdr_t {
    size_t size;  // this chunk size in bytes
    size_t allocated;  // amount of allocated bytes in this chunk
    marena_chunk_hdr_t *next;  // next chunk ptr
};

// returnable memory block header (size aligned go GRANULARITY)
typedef struct _marena_rt_hdr_t marena_rt_hdr_t;
struct _marena_rt_hdr_t {
    marena_rt_hdr_t *next; // ptr to next free block
    size_t size; // size of this block (including header size)
    size_t magic; // magic number for correctness test
};

// memory arena object (size aligned go GRANULARITY)
typedef struct _marena_t marena_t;
struct _marena_t {
    size_t chunk_size;  // arena chunk size
    marena_chunk_hdr_t *first;  // first arena memory chunk
    marena_chunk_hdr_t *curr;  // current arena memory chunk
    marena_rt_hdr_t *free; // ptr to first free returnable block
};

// Create arena chunk.
static marena_chunk_hdr_t *marena_create_chunk(size_t size)
{
    ROUNDUP(size);
    marena_chunk_hdr_t *ret = malloc(sizeof(marena_chunk_hdr_t) + size);
    if (ret) {
        ret->size = size;
        ret->allocated = 0;
        ret->next = NULL;
    }
    return ret;
}

// Create memory arena object.
static marena_t *marena_create(size_t size)
{
    ROUNDUP(size);
    marena_t *ret = malloc(sizeof(marena_t));
    if (ret) {
        marena_chunk_hdr_t *chunk = marena_create_chunk(size);
        if (!chunk) {
            free(ret);
            return NULL;
        }
        ret->chunk_size = size;
        ret->first = chunk;
        ret->curr = chunk;
        ret->free = NULL;
    }
    return ret;
}

// Destroy memory arena object.
static void marena_destroy(marena_t *ma)
{
    marena_chunk_hdr_t *curr = ma->first;
    while (curr) {
        marena_chunk_hdr_t *next = curr->next;
        free(curr);
        curr = next;
    }
    free(ma);
}

// Allocate memory from arena.
static void *marena_alloc(marena_t *ma, size_t size)
{
    char *ret = NULL;

    if (ma == NULL)
        goto exit;

    ROUNDUP(size);
    if (ma->curr->allocated + size > ma->curr->size) {
        if (ma->curr->next) {
            ma->curr = ma->curr->next;
            ma->curr->allocated = 0;
        } else {
            marena_chunk_hdr_t *chunk = marena_create_chunk(ma->chunk_size);
            if (!chunk)
                goto exit;
            ma->curr->next = chunk;
            ma->curr = chunk;
        }
    }

    ret = (char*)ma->curr + sizeof(marena_chunk_hdr_t) + ma->curr->allocated;
    ma->curr->allocated += size;

exit:
    return ret;
}

// Free all memory arena.
static void marena_free(marena_t *ma)
{
    if (ma == NULL)
        return;

    ma->curr = ma->first;
    ma->curr->allocated = 0;

    ma->free = NULL;
}

// Allocate returnable block from memory arena.
static void *marena_alloc_rt(marena_t *ma, size_t size)
{
    char *ret = NULL;

    // add header size
    ROUNDUP(size);
    size += sizeof(marena_rt_hdr_t);

    // try to allocate returnable block from prevously freed
    marena_rt_hdr_t **prev = &ma->free;
    marena_rt_hdr_t *curr;
    for (curr = *prev; curr; prev = &curr->next, curr = curr->next)
        if (curr->size >= size) {
            if (curr->size >= 2 * size) { // split block in two
                curr->size -= size;
                marena_rt_hdr_t *spl = (void*)((char*)curr + curr->size);
                spl->next = 0;
                spl->size = size;
                spl->magic = MAGIC;
                ret = (char*)(spl + 1);
            } else { // allocate all block
                *prev = curr->next;
                ret = (char*)(curr + 1);
            }
            goto exit;
        }

    // try to allocate from arena
    curr = marena_alloc(ma, size);
    if (!curr)
        goto exit;
    curr->next = NULL;
    curr->size = size;
    curr->magic = MAGIC;
    ret = (char*)(curr + 1);

exit:
    return ret;
}

// Resize returnable block.
static void *marena_realloc_rt(marena_t *ma, void *ptr, size_t size)
{
    char *ret = NULL;

    if (ma == NULL)
        goto exit;

    // add header size
    ROUNDUP(size);
    size += sizeof(marena_rt_hdr_t);

    // check if ptr really points to returnable block
    marena_rt_hdr_t *curr = (marena_rt_hdr_t*)ptr - 1;
    if (curr->magic != MAGIC)
        goto exit;

    // check if block already has requred size
    if (curr->size >= size) {
        ret = ptr;
        goto exit;
    }

    // allocate new returnable block
    ret = marena_alloc_rt(ma, size);
    if (ret == NULL)
        goto exit;

    // copy data to new block
    memcpy(ret, ptr, curr->size);

    // free old block
    curr->next = ma->free;
    ma->free = curr;

exit:
    return ret;
}

// Free returnable block.
static void marena_free_rt(marena_t *ma, void *ptr)
{
    if (ma == NULL)
        goto exit;

    // check if ptr really points to returnable block
    marena_rt_hdr_t *curr = (marena_rt_hdr_t*)ptr - 1;
    if (curr->magic != MAGIC)
        goto exit;

    // free block
    curr->next = ma->free;
    ma->free = curr;

exit:
    return;
}


/*****************************************************************************
* Json object attribute name table.
*****************************************************************************/

// Attribute name index type.
typedef ushort ani_t;

// Maximum number of attribute names in json.
#define ANI_MAX 0xFFFF

// Attribute names table object.
typedef struct _ant_t {
    marena_t *mem; // memory allocator

    // array of attribute names
    const char **an; // attribute names
    uint an_cnt; // count
    uint an_cap; // capacity

    // hash table (mapping between names and their indexes)
    const char **ht_an; // attribute names
    ani_t *ht_ani; // attribute name indexes
    uint ht_size; // size
} ant_t;

// Create attribute names table object.
static ant_t *ant_create(marena_t *mem)
{
    ant_t *ant = marena_alloc(mem, sizeof(*ant));
    if (ant == NULL)
        return NULL;
    ant->mem = mem;

    ant->an_cap = 8;
    ant->an = marena_alloc_rt(mem, ant->an_cap * sizeof(ant->an[0]));
    if (ant->an == NULL)
        return NULL;

    ant->an_cnt = 1; // attribute name indexes should start from 1
    ant->an[0] = NULL; // because 0 can not be put in hash table

    uint hts = ant->an_cap * 4;
    ant->ht_an = marena_alloc_rt(mem, hts * sizeof(ant->ht_an[0]));
    if (ant->ht_an == NULL)
        return NULL;
    memset(ant->ht_an, 0, hts * sizeof(ant->ht_an[0]));
    ant->ht_ani = marena_alloc_rt(mem, hts * sizeof(ant->ht_ani[0]));
    if (ant->ht_ani == NULL)
        return NULL;
    ant->ht_size = hts;

    return ant;
}

// Calculate hash value of a string.
static inline uint ant_hash(const char *s)
{
    uint h = 0;
    for (; *s; s++)
        h = h * 7879 + (h >> 16) + *(const uchar*)s;
    return h;
}

// Set hash table element.
static void ant_set(ant_t *ant, const char *name, int index)
{
    uint i = ant_hash(name) % ant->ht_size;
    while (ant->ht_an[i]) {
        if (++i >= ant->ht_size)
            i = 0;
    }
    ant->ht_an[i] = name;
    ant->ht_ani[i] = (ani_t)index;
}

// Find hash table element.
static int ant_get(ant_t *ant, const char *name)
{
    uint i = ant_hash(name) % ant->ht_size;
    while (ant->ht_an[i]) {
        if (0 == strcmp(name, ant->ht_an[i]))
            return ant->ht_ani[i];
        if (++i >= ant->ht_size)
            i = 0;
    }
    return -1;
}

// Add new or search for existing token.
static int ant_add_token(ant_t *ant, const char *start, uint len)
{
    uint i;

    // copy name to buffer adding 0 at end
    char name[64];
    if (len >= sizeof(name)) {
        ERROR("attribute name too long");
        goto exit;
    }
    memcpy(name, start, len);
    name[len] = 0;

    // check if attribute name is already present
    int index = ant_get(ant, name);
    if (index >= 0)
        return index;

    // allocate memory for attribute name
    char *p = marena_alloc(ant->mem, len + 1);
    if (!p)
        goto exit;
    memcpy(p, name, len + 1);

    // need to resize array and hash table
    if (ant->an_cnt >= ant->an_cap) {
        // grow array of attribute names
        ant->an_cap *= 2;
        ant->an = marena_realloc_rt(ant->mem, ant->an,
            ant->an_cap * sizeof(ant->an[0]));
        if (ant->an == NULL)
            goto exit;

        // grow hash table
        const char **old_an = ant->ht_an;
        ani_t *old_ani = ant->ht_ani;
        uint old_size = ant->ht_size;
        uint hts = ant->an_cap * 4;
        ant->ht_an = marena_alloc_rt(ant->mem, hts * sizeof(ant->ht_an[0]));
        if (ant->ht_an == NULL)
            goto exit;
        memset(ant->ht_an, 0, hts * sizeof(ant->ht_an[0]));
        ant->ht_ani = marena_alloc_rt(ant->mem, hts * sizeof(ant->ht_ani[0]));
        if (ant->ht_ani == NULL)
            goto exit;
        ant->ht_size = hts;
        for (i = 0; i < old_size; i++) // do rehashing
            if (old_an[i])
                ant_set(ant, old_an[i], old_ani[i]);
        marena_free_rt(ant->mem, old_an);
        marena_free_rt(ant->mem, old_ani);
    }

    index = (int)ant->an_cnt++;
    ant->an[index] = p;
    ant_set(ant, p, index);
    return index;

exit:
    return -1;
}


/*****************************************************************************
* Hash table for mapping between attribute name index and node array index.
*****************************************************************************/

// Hash table object.
typedef struct _ht_t {
    ani_t *k; // attribute name indexes
    void *v; // node array indexes
    int num; // number of elements in hash table
    int size; // size of hash table
} ht_t;

// Create hash table object.
static void *ht_create(marena_t *mem, int cnt)
{
    ht_t *ht = marena_alloc(mem, sizeof(*ht));
    if (ht == NULL)
        return NULL;

    ht->num = cnt;
    ht->size = cnt * 4;

    uint sk = (uint)ht->size * sizeof(ht->k[0]);
    ht->k = marena_alloc(mem, sk);
    if (ht->k == NULL)
        return NULL;
    memset(ht->k, 0, sk);

    uint sv = (uint)ht->size * (ht->num < 256 ? sizeof(uchar) : sizeof(ani_t));
    ht->v = marena_alloc(mem, sv);
    if (ht->v == NULL)
        return NULL;
    return ht;
}

// Set hash table element.
static void ht_set(ht_t *ht, ani_t k, int v)
{
    int i = (int)k % ht->size;
    while (ht->k[i]) {
        if (ht->k[i] == k)
            break;
        if (++i >= ht->size)
            i = 0;
    }
    ht->k[i] = k;
    if (ht->num < 256) {
        uchar *arr = ht->v;
        arr[i] = (uchar)v;
    } else {
        ani_t *arr = ht->v;
        arr[i] = (ani_t)v;
    }
}

// Find hash table element.
static int ht_get(ht_t *ht, ani_t k)
{
    int i = (int)k % ht->size;
    while (ht->k[i]) {
        if (ht->k[i] == k) {
            if (ht->num < 256) {
                uchar *arr = ht->v;
                return arr[i];
            } else {
                ani_t *arr = ht->v;
                return arr[i];
            }
        }
        if (++i >= ht->size)
            i = 0;
    }
    return -1;
}


/*****************************************************************************
* Json parser data and functions.
*****************************************************************************/

// Minimal size of arena allocator.
#define JSON_MEM_MIN (16 * 1024)

// Minimal count of stack elements (determines possible json nesting).
#define JSON_STACK_MIN 16

// Initial capacity of dynamic arrays.
#define JSON_CAP_MIN 8

// Character types.
enum {
    CNV, // invalid characters
    CBL, // blank symbols ' ', '\t', '\n', '\r'
    CMN, // minus '-'
    CPT, // point
    CNM, // numbers '0-9'
    CLT, // letters '_', 'a-z', 'A-Z'
    CQT, // quotes "'", '"'
    CCM, // comma ','
    CCL, // colon ':'
    CAS, // array start '['
    CAE, // array end ']'
    COS, // object start '{'
    COE, // object end '}'
    CSL // slash '/'
};

// Token types.
typedef enum {
    JINSTART, // input start
    JINEND, // input end
    JASTART, // '[' - array start
    JAEND, // ']' - array end
    JOSTART, // '{' - object start
    JOEND, // '}' - object end
    JCOMMA, // ',' - comma
    JNULL, // null
    JBOOL, // true or false
    JINT, // integer number
    JDBL, // double-precision floating-point number
    JSTR, // string
    JNAME, // object attribute name
    JERROR // none of the above
} jtt;

// Parsing context.
typedef enum {
    CTXVAL, // parsing singular value
    CTXARR, // parsing json array
    CTXOBJ // parsing json object
} jctx;

// Token object.
typedef struct {
    jtt type; // token type
    uint len; // token length
    uint pos; // position inside json string
} jtok;

// Json object node.
typedef struct _jnode_obj_t {
    jnode_t b; // inheritance from base type
    ant_t *ant; // ptr to attribute names table
    union {
        ani_t *anis; // array of attribute name indexes
        ht_t *ht; // ptr to hash table
    };
} jnode_obj_t;

// Stack element of parser.
typedef struct {
    jtok tokp; // previous token
    jctx ctx; // parsing context
    jnode_t *node; // current json node
    int node_cap; // capacity of all arrays for current node
} jpstk;

// Json parser object.
struct _jparser_t {
    marena_t *mem; // memory allocator

    const char *start; // json string start
    uint len; // json string length
    uint pos; // current position in json string

    ant_t *ant; // attribute names table

    jnode_t **root; // ptr to root node ptr
    jtok tokc; // current token

    uint sidx; // stack index
    jpstk *stack; // stack
    uint ssize; // stack size
};

// Character type translation table.
static uchar ct[256] = {
    //0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F  //
    CNV, CNV, CNV, CNV, CNV, CNV, CNV, CNV, CNV, CBL, CBL, CNV, CNV, CBL, CNV, CNV, // 00
    CNV, CNV, CNV, CNV, CNV, CNV, CNV, CNV, CNV, CNV, CNV, CNV, CNV, CNV, CNV, CNV, // 10
    CBL, CNV, CQT, CNV, CNV, CNV, CNV, CQT, CNV, CNV, CNV, CNV, CCM, CMN, CPT, CSL, // 20
    CNM, CNM, CNM, CNM, CNM, CNM, CNM, CNM, CNM, CNM, CCL, CNV, CNV, CNV, CNV, CNV, // 30
    CNV, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, // 40
    CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CAS, CNV, CAE, CNV, CLT, // 50
    CNV, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, // 60
    CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, CLT, COS, CNV, COE, CNV, CNV // 70
};

// Forward declarations.
static jnode_t *jp_new_node(jparser_t *jp, jtype_t type);
static int jp_add_elt(jparser_t *jp, jnode_t *n);
static int jp_add_attr(jparser_t *jp, ani_t index);
static int jp_obj_end(jparser_t *jp);
static void jp_next(jparser_t *jp);
static const char *jp_read_str(jparser_t *jp, int *len);

// Json node for returning absent values.
static jnode_t none;

/* Get node from array node by index.
 *
 * In:
 *      node - json node of type JT_ARR
 *      i - array index
 * Return:
 *      json node
 */
jnode_t *jn_elt(jnode_t *node, int i)
{
    if (node->type != JT_ARR)
        return &none;

    if (!(0 <= i && i < node->elts.count))
        return &none;

    return node->elts.values[i];
}

/* Get node from object node by attribute name.
 * Attribute names are case sensitive.
 * Searching is done using hash tables.
 *
 * In:
 *      node - json node of type JT_OBJ
 *      name - object attribute name
 * Return:
 *      json node
 */
jnode_t *jn_attr(jnode_t *node, const char *name)
{
    if (node->type != JT_OBJ)
        return &none;

    // get attribute name index
    jnode_obj_t *nobj = (jnode_obj_t*)node;
    int i = ant_get(nobj->ant, name);
    if (i < 0)
        return &none;

    // get array index
    i = ht_get(nobj->ht, (ani_t)i);
    if (i < 0)
        return &none;

    return node->attrs.values[i];
}

/* Create json parser object.
 * All memory is allocated here and during parsing there are no
 * calls to malloc() or free().
 *
 * In:
 *      jp[out] - address of ptr to json parser object
 *      mem - amount of memory to be used for parsing;
 *            if 0, then default value is used
 *      stack - stack depth; this value controls maximum nesting in json;
 *              if 0 then default value is used
 * Return:
 *      0 - success
 *      !0 - error
 */
int jp_create(jparser_t **jp, size_t mem, size_t stack)
{
    int ret = -1;

    // adjust input values
    if (mem < JSON_MEM_MIN)
        mem = JSON_MEM_MIN;
    if (stack < JSON_STACK_MIN)
        stack = JSON_STACK_MIN;

    // get memory for parser object
    jparser_t *p = malloc(sizeof(*p));
    if (!p)
        goto enomem;
    memset(p, 0, sizeof(*p));

    // get memory for stack
    p->stack = malloc(stack * sizeof(p->stack[0]));
    if (!p->stack)
        goto enomem;
    p->ssize = (uint)stack;

    // get memory for allocator
    p->mem = marena_create(mem);
    if (!p->mem)
        goto enomem;

    *jp = p;
    ret = 0;

exit:
    return ret;

enomem:
    ERROR("no memory");
    if (p) {
        free(p->stack);
        free(p);
    }
    goto exit;
}

/* Destroy json parser object and release all allocated memory.
 *
 * In:
 *      jp - ptr to json parser object
 */
void jp_destroy(jparser_t *jp)
{
    if (jp == NULL)
        return;

    marena_destroy(jp->mem);
    free(jp->stack);
    free(jp);
}

/* Parse json string into a tree of 'jnode_t' structures.
 * These structures need not to be freed manually. They are freed
 * automatically then jp_parse() is called next time or
 * json parser object is destroyed.
 *
 * In:
 *      jp - ptr to json parser object
 *      root[out] - addres of ptr to root node
 *      json - ptr to json string
 *      len - length of json string
 *
 * Return:
 *      0 - success
 *      !0 - error
 */
int jp_parse(jparser_t *jp, jnode_t **root, const char *json, size_t len)
{
    jnode_t *n = NULL;

    marena_free(jp->mem);

    jp->start = json;
    jp->len = (uint)len;
    jp->pos = 0;

    jp->ant = ant_create(jp->mem);
    if (!jp->ant) {
        ERROR("no memory");
        return -1;
    }

    *root = &none;
    jp->root = root;

    jp->tokc.type = JINSTART;
    jp->tokc.pos = 0;
    jp->tokc.len = 0;

    jp->sidx = 0; // stack index
    jpstk *s = jp->stack; // stack pointer
    s->ctx = CTXVAL;

    for (;;) {
        s->tokp = jp->tokc;
        jtt t = s->tokp.type;
        jp_next(jp);
        switch (jp->tokc.type) {
        case JINEND:
            if (s->ctx != CTXVAL)
                return -1;
            if (t == JINSTART)
                return -1;
            return 0;
        case JASTART:
            n = jp_new_node(jp, JT_ARR);
            if (n == NULL)
                return -1;
            if (++jp->sidx >= jp->ssize)
                return -1;
            s++;
            s->node = n;
            s->node_cap = JSON_CAP_MIN;
            s->ctx = CTXARR;
            break;
        case JOSTART:
            n = jp_new_node(jp, JT_OBJ);
            if (n == NULL)
                return -1;
            if (++jp->sidx >= jp->ssize)
                return -1;
            s++;
            s->node = n;
            s->node_cap = JSON_CAP_MIN;
            s->ctx = CTXOBJ;
            break;
        case JAEND:
            if (s->ctx != CTXARR)
                return -1;
            if (t == JCOMMA)
                return -1;
            if (jp->sidx == 0)
                return -1;
            jp->sidx--;
            s--;
            break;
        case JOEND:
            if (s->ctx != CTXOBJ)
                return -1;
            if (t == JCOMMA || t == JNAME)
                return -1;
            if (jp_obj_end(jp))
                return -1;
            if (jp->sidx == 0)
                return -1;
            jp->sidx--;
            s--;
            break;
        case JCOMMA:
            if (s->ctx == CTXVAL) {
                return -1;
            } else if (s->ctx == CTXARR) {
                if (t == JASTART)
                    return -1;
            } else {
                if (t == JOSTART || t == JNAME)
                    return -1;
            }
            break;
        case JNULL:
            n = jp_new_node(jp, JT_NULL);
            if (n == NULL)
                return -1;
            break;
        case JBOOL:
            n = jp_new_node(jp, JT_BOOL);
            if (n == NULL)
                return -1;
            break;
        case JINT:
            n = jp_new_node(jp, JT_INT);
            if (n == NULL)
                return -1;
            break;
        case JDBL:
#if JSON_DOUBLE == 1
            n = jp_new_node(jp, JT_DBL);
            if (n == NULL)
                return -1;
            break;
#else
            return -1;
#endif
        case JSTR:
            n = jp_new_node(jp, JT_STR);
            if (n == NULL)
                return -1;
            break;
        case JNAME:
            if (s->ctx != CTXOBJ)
                return -1;
            if (t != JOSTART && t != JCOMMA)
                return -1;
            jtok *t = &jp->tokc;
            int i = ant_add_token(jp->ant, jp->start + t->pos, t->len);
            if (i < 0)
                return -1;
            if (jp_add_attr(jp, (ani_t)i))
                return -1;
            break;
        default:
            return -1;
        }
    }
}

// Create new json node.
static jnode_t *jp_new_node(jparser_t *jp, jtype_t type)
{
    uint ns = (type == JT_OBJ) ? sizeof(jnode_obj_t) : sizeof(jnode_t);
    jnode_t *n = marena_alloc(jp->mem, ns);
    if (n == NULL) {
        ERROR("no memory");
        return NULL;
    }
    memset(n, 0, ns);
    n->type = type;

    // set node value
    if (type == JT_BOOL) {
        const char *p = jp->start + jp->tokc.pos;
        n->bool_val = ((*p | 0x20) == 't');
    } else if (type == JT_INT) {
        n->int_val = atoi(jp->start + jp->tokc.pos);
#if JSON_DOUBLE == 1
    } else if (type == JT_DBL) {
        n->dbl_val = strtod(jp->start + jp->tokc.pos, NULL);
#endif
    } else if (type == JT_STR) {
        n->str_val = jp_read_str(jp, &n->str_len);
        if (n->str_val == NULL)
            return NULL;
    } else if (type == JT_ARR) {
        size_t size = JSON_CAP_MIN * sizeof(n->elts.values[0]);
        void *arr = marena_alloc_rt(jp->mem, size);
        if (arr == NULL)
            return NULL;
        n->elts.values = arr;
    } else if (type == JT_OBJ) {
        jnode_obj_t *nobj = (jnode_obj_t*)n;
        size_t size = JSON_CAP_MIN * sizeof(n->attrs.values[0]);
        void *arr = marena_alloc_rt(jp->mem, size);
        if (arr == NULL)
            return NULL;
        n->attrs.values = arr;
        nobj->ant = jp->ant;
        size = JSON_CAP_MIN * sizeof(nobj->anis[0]);
        arr = marena_alloc_rt(jp->mem, size);
        if (arr == NULL)
            return NULL;
        nobj->anis = arr;
    }

    // store new node to parent node or set it as a root node
    jpstk *s = jp->stack + jp->sidx;
    jtt t = s->tokp.type;
    if (s->ctx == CTXVAL) {
        if (t != JINSTART)
            return NULL;
        *jp->root = n;
    } else if (s->ctx == CTXARR) {
        if (t != JASTART && t != JCOMMA)
            return NULL;
        if (jp_add_elt(jp, n))
            return NULL;
    } else {
        if (t != JNAME)
            return NULL;
        int i = s->node->attrs.count - 1;
        s->node->attrs.values[i] = n;
    }

    return n;
}

// Add node to current array node.
static int jp_add_elt(jparser_t *jp, jnode_t *node)
{
    jpstk *s = jp->stack + jp->sidx;
    jnode_t *n = s->node;

    if (n->elts.count >= s->node_cap) {
        s->node_cap *= 2;
        size_t size = (size_t)s->node_cap * sizeof(n->elts.values[0]);
        n->elts.values = marena_realloc_rt(jp->mem, n->elts.values, size);
        if (!n->elts.values)
            return -1;
    }

    n->elts.values[n->elts.count++] = node;
    return 0;
}

// Add attribute name to current object node.
static int jp_add_attr(jparser_t *jp, ani_t index)
{
    jpstk *s = jp->stack + jp->sidx;
    jnode_obj_t *n = (jnode_obj_t*)s->node;

    if (n->b.attrs.count >= s->node_cap) {
        s->node_cap *= 2;

        size_t size = (size_t)s->node_cap * sizeof(n->anis[0]);
        n->anis = marena_realloc_rt(jp->mem, n->anis, size);
        if (!n->anis)
            return -1;

        size = (size_t)s->node_cap * sizeof(n->b.attrs.values[0]);
        n->b.attrs.values = marena_realloc_rt(jp->mem, n->b.attrs.values, size);
        if (!n->b.attrs.values)
            return -1;
    }

    n->anis[n->b.attrs.count] = index;
    n->b.attrs.values[n->b.attrs.count++] = NULL;
    return 0;
}

// Finish creation of object node.
static int jp_obj_end(jparser_t *jp)
{
    jpstk *s = jp->stack + jp->sidx;
    jnode_obj_t *n = (jnode_obj_t*)s->node;
    ani_t *anis = n->anis;

    // allocate and fill array of attribute names
    uint size = (uint)n->b.attrs.count * sizeof(n->b.attrs.names[0]);
    n->b.attrs.names = marena_alloc(jp->mem, size);
    if (!n->b.attrs.names)
        return -1;
    for (int i = 0; i < n->b.attrs.count; i++)
        n->b.attrs.names[i] = jp->ant->an[anis[i]];

    // allocate and fill hash table
    n->ht = ht_create(jp->mem, n->b.attrs.count);
    if (!n->ht)
        return -1;
    for (int i = 0; i < n->b.attrs.count; i++)
        ht_set(n->ht, anis[i], i);

    marena_free_rt(jp->mem, anis);
    return 0;
}

// Compare ignoring case.
static inline bool jp_cmpi(const uchar *s1, const char *s2)
{
    for (int i = 0; s2[i]; i++)
        if ((s1[i] | 0x20) != (uchar)s2[i])
            return false;
    return true;
}

// Read next token from json stream.
static void jp_next(jparser_t *jp)
{
    const uchar *jsn = (const uchar*)jp->start;
    const uint len = jp->len;
    uint pos = jp->pos;
    jtok *tok = &jp->tokc;
    int t = CNV;

    // skip spaces
    for (; pos < len; pos++) {
        t = ct[jsn[pos]];
        if (t != CBL)
            break;
    }

    // check for input end
    if (pos >= len) {
        tok->type = JINEND;
        goto exit;
    }

    // check for array start
    if (t == CAS) {
        tok->type = JASTART;
        goto exit1;
    }

    // check for array end
    if (t == CAE) {
        tok->type = JAEND;
        goto exit1;
    }

    // check for object start
    if (t == COS) {
        tok->type = JOSTART;
        goto exit1;
    }

    // check for object end
    if (t == COE) {
        tok->type = JOEND;
        goto exit1;
    }

    // check for comma
    if (t == CCM) {
        tok->type = JCOMMA;
        goto exit1;
    }

    // check for number
    if (t == CMN || t == CNM) {
        tok->type = JINT;
        tok->pos = pos++;
        tok->len = 1;
        for (; pos < len; pos++, tok->len++) {
            t = ct[jsn[pos]];
            if (t != CNM)
                break;
        }
        if (pos < len) {
            if (jsn[pos] == '.') {
                tok->type = JDBL;
                tok->len++;
                if (++pos >= len)
                    goto error;
                t = ct[jsn[pos]];
                if (t != CNM)
                    goto error;
                pos++;
                tok->len++;
                for (; pos < len; pos++, tok->len++) {
                    t = ct[jsn[pos]];
                    if (t != CNM)
                        break;
                }
                if (pos >= len)
                    goto exit;
            }
            if ((jsn[pos] | 0x20) == 'e') {
                tok->type = JDBL;
                tok->len++;
                if (++pos >= len)
                    goto error;
                if (jsn[pos] == '-' || jsn[pos] == '+') {
                    tok->len++;
                    if (++pos >= len)
                        goto error;
                }
                t = ct[jsn[pos]];
                if (t != CNM)
                    goto error;
                tok->len++;
                pos++;
                for (; pos < len; pos++, tok->len++) {
                    t = ct[jsn[pos]];
                    if (t != CNM)
                        break;
                }
            }
        }
        if (tok->type == JINT) { // check if value is in int range
            if (tok->len > 10)
                tok->type = JDBL;
            else if (tok->len == 10) {
                const char *val = jp->start + tok->pos;
                const char *max = "2147483647";
                if (*val == '-') {
                    val++;
                    max = "2147483648";
                }
                for (int i = 0; i < 10; i++)
                    if (val[i] > max[i]) {
                        tok->type = JDBL;
                        break;
                    }
            }
        }
        goto exit;
    }

    // check for string or an object attribute name with quotes
    if (t == CQT) {
        int c = jsn[pos];
        tok->type = JERROR;
        tok->pos = ++pos;
        tok->len = 0;
        for (; pos < len; pos++, tok->len++) {
            if (jsn[pos] == c && jsn[pos - 1] != '\\') {
                tok->type = JSTR;
                pos++;
                break;
            }
        }
        for (; pos < len; pos++) {
            t = ct[jsn[pos]];
            if (t != CBL)
                break;
        }
        if (pos >= len)
            goto exit;
        if (t == CCL) {
            tok->type = JNAME;
            pos++;
            if (ct[jsn[tok->pos]] != CLT)
                goto error;
            uint i = 1;
            for (; i < tok->len; i++) {
                t = ct[jsn[tok->pos + i]];
                if (t != CLT && t != CNM)
                    goto error;
            }
        }
        goto exit;
    }

    // check for 'null'
    if ((t == CLT) && (pos + 4 <= len) && jp_cmpi(jsn + pos, "null")) {
        tok->type = JNULL;
        tok->pos = pos;
        tok->len = 4;
        pos += 4;
        goto exit;
    }

    // check for 'true'
    if ((t == CLT) && (pos + 4 <= len) && jp_cmpi(jsn + pos, "true")) {
        tok->type = JBOOL;
        tok->pos = pos;
        tok->len = 4;
        pos += 4;
        goto exit;
    }

    // check for 'false'
    if ((t == CLT) && (pos + 5 <= len) && jp_cmpi(jsn + pos, "false")) {
        tok->type = JBOOL;
        tok->pos = pos;
        tok->len = 5;
        pos += 5;
        goto exit;
    }

    // check for object attribute name without quotes
    if (t == CLT) {
        tok->type = JNAME;
        tok->pos = pos++;
        tok->len = 1;
        for (; pos < len; pos++) {
            t = ct[jsn[pos]];
            if (t != CLT && t != CNM)
                break;
            tok->len++;
        }
        for (; pos < len; pos++) {
            t = ct[jsn[pos]];
            if (t != CBL)
                break;
        }
        if (pos >= len)
            goto error;
        if (t != CCL)
            goto error;
        goto exit1;
    }

    // if none of the above than error
    goto error;

exit1:
    pos++;
exit:
    jp->pos = pos;
    return;

error:
    tok->type = JERROR;
    return;
}

// Copy string from json to C.
static const char *jp_read_str(jparser_t *jp, int *len)
{
    uint ssize = jp->tokc.len; // src string size
    const char *s = jp->start + jp->tokc.pos; // src string
    uint dsize = 8; // dst string size
    char *d = marena_alloc_rt(jp->mem, dsize); // dst string
    if (d == NULL)
        return NULL;

    uint si, di;
    for (si = di = 0; si < ssize; si++) {
        // realloc space for dst string if needed
        if (di >= dsize - 1) {
            dsize *= 2;
            d = marena_realloc_rt(jp->mem, d, dsize);
            if (d == NULL)
                return NULL;
        }

        // check for escape character
        char c = s[si];
        if (c != '\\') {
            d[di++] = c;
            continue;
        }

        // do unescaping
        c = s[++si];
        if (c == '"')
            d[di++] = '"';
        else if (c == '\\')
            d[di++] = '\\';
        else if (c == '/')
            d[di++] = '/';
        else if (c == 'b')
            d[di++] = '\b';
        else if (c == 'f')
            d[di++] = '\f';
        else if (c == 'n')
            d[di++] = '\n';
        else if (c == 'r')
            d[di++] = '\r';
        else if (c == 't')
            d[di++] = '\t';
        else {
            d[di++] = '\\';
            d[di++] = c;
        }
    }

    d[di] = 0;
    *len = (int)di;
    return d;
}


/*****************************************************************************
* Json writer data and functions.
*****************************************************************************/

// Stack element of json writer.
typedef struct {
    jctx ctx; // parsing context
    jtt tt; // current token type
} jwstk;

// Json writer object.
struct _jwriter_t {
    char *start; // json string start
    uint len; // json string length
    uint pos; // current position in json string
    int err; // error flag

    jwstk *stack; // stack
    uint ssize; // stack size
    uint sidx; // stack index
};

// Write zero-terminated string to json buffer.
static void jw_strz(jwriter_t *jw, const char *str)
{
    for (;;) {
        char c = *str++;
        if (c == 0)
            break;
        if (jw->pos >= jw->len - 1) {
            ERROR("buffer too small");
            jw->err = 1;
            return;
        }
        jw->start[jw->pos++] = c;
    }
}

// Write zero-terminated string to json buffer using format string.
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
static void jw_printf(jwriter_t *jw, const char *fmt, ...)
{
    va_list v;
    va_start(v, fmt);
    char *buf = jw->start + jw->pos;
    size_t size = jw->len - jw->pos - 1;
    int res = vsnprintf(buf, size, fmt, v);
    va_end(v);
    if (res < 0) {
        if (errno == ERANGE) {
            ERROR("buffer too small");
        } else {
            ERROR("vsnprintf() error %d '%s'", errno, strerror(errno));
        }
        jw->err = 1;
    } else {
        if ((size_t)res > size) {
            ERROR("buffer too small");
            res = (int)size;
            jw->err = 1;
        }
        jw->pos += (uint)res;
    }
}
#pragma GCC diagnostic warning "-Wformat-nonliteral"

// Prepare for writing a value to json string.
static int jw_prepv(jwriter_t *jw, const char *name)
{
    if (!jw)
        return -1;

    jwstk *s = jw->stack + jw->sidx;

    if (s->ctx == CTXOBJ && name == NULL) {
        ERROR("need attribute name");
        jw->err = 1;
        return -1;
    } else if (s->ctx != CTXOBJ && name != NULL) {
        ERROR("attribute name should be NULL");
        jw->err = 1;
        return -1;
    }

    if (s->ctx == CTXVAL) {
        if (s->tt != JINSTART) {
            jw->err = 1;
        }
    } else if (s->ctx == CTXARR) {
        if (s->tt != JASTART) {
            jw_strz(jw, ",");
        }
    } else { // object
        if (s->tt != JOSTART) {
            jw_strz(jw, ",");
        }
        jw_printf(jw, "\"%s\":", name);
    }

    return jw->err;
}

/* Create json writer object.
 *
 * In:
 *      jw[out] - address of ptr to json writer object
 *      mem - amount of memory to be used for writing;
 *            if 0, then default value is used
 *      stack - stack depth; this value controls maximum nesting in json;
 *              if 0 then default value is used
 * Return:
 *      0 - success
 *      !0 - error
 */
int jw_create(jwriter_t **jw, size_t mem, size_t stack)
{
    int ret = 1;

    // adjust input values
    if (mem < JSON_MEM_MIN)
        mem = JSON_MEM_MIN;
    if (stack < JSON_STACK_MIN)
        stack = JSON_STACK_MIN;

    // get memory for writer object
    jwriter_t *p = malloc(sizeof(*p));
    if (!p)
        goto enomem;
    memset(p, 0, sizeof(*p));

    // get memory for stack
    p->stack = malloc(stack * sizeof(p->stack[0]));
    if (!p->stack)
        goto enomem;
    p->ssize = (uint)stack;

    // get memory for json string
    p->start = malloc(mem);
    if (!p->start)
        goto enomem;
    p->len = (uint)mem;

    *jw = p;
    ret = 0;

exit:
    return ret;

enomem:
    ERROR("no memory");
    if (p) {
        free(p->stack);
        free(p);
    }
    goto exit;
}

/* Destroy json writer object.
 *
 * In:
 *      jw - ptr to json writer object
 */
void jw_destroy(jwriter_t *jw)
{
    if (!jw)
        return;

    free(jw->start);
    free(jw->stack);
    free(jw);
}

/* Begin json writing.
 * Previously written json string is discarded.
 *
 * In:
 *      jw - ptr to json writer object
 */
void jw_begin(jwriter_t *jw)
{
    if (!jw)
        return;

    jw->pos = 0;
    jw->err = 0;
    jw->sidx = 0;

    jwstk *s = jw->stack + jw->sidx;
    s->ctx = CTXVAL;
    s->tt = JINSTART;
}

/* Get written json string.
 * String is zero terminated. String is located in json writer object
 * memory and need not to be freed manually.
 *
 * In:
 *      jw - ptr to json writer object
 *      str[out] - address of json string start
 *      size[out] - address of json string size
 * Return:
 *      0 - success
 *      !0 - error
 */
int jw_get(jwriter_t *jw, char **str, size_t *size)
{
    if (!jw)
        return -1;
    if (jw->pos >= jw->len)
        return -1;
    if (jw->stack[jw->sidx].ctx != CTXVAL)
        return -1;

    jw->start[jw->pos] = 0;
    *str = jw->start;
    if (size)
        *size = jw->pos;

    return jw->err;
}

/* Write null value to json writer.
 * Possible errors are not reported until call to jw_get().
 *
 * In:
 *      jw - ptr to json writer object
 *      name - object attribute name if writing is done inside object context;
 *             must be NULL if writing is done inside array context
 */
void jw_null(jwriter_t *jw, const char *name)
{
    if (jw_prepv(jw, name))
        return;
    jw_strz(jw, "null");
    jw->stack[jw->sidx].tt = JNULL;
}

/* Write boolean value to json writer.
 * Possible errors are not reported until call to jw_get().
 *
 * In:
 *      jw - ptr to json writer object
 *      val - boolean value
 *      name - object attribute name if writing is done inside object context;
 *             must be NULL if writing is done inside array context
 */
void jw_bool(jwriter_t *jw, bool val, const char *name)
{
    if (jw_prepv(jw, name))
        return;
    jw_strz(jw, (val ? "true" : "false"));
    jw->stack[jw->sidx].tt = JBOOL;
}

/* Write integer value to json writer.
 * Possible errors are not reported until call to jw_get().
 *
 * In:
 *      jw - ptr to json writer object
 *      val - integer value
 *      name - object attribute name if writing is done inside object context;
 *             must be NULL if writing is done inside array context
 */
void jw_int(jwriter_t *jw, int val, const char *name)
{
    if (jw_prepv(jw, name))
        return;
    jw_printf(jw, "%d", val);
    jw->stack[jw->sidx].tt = JINT;
}

#if JSON_DOUBLE == 1
/* Write double value to json writer.
 * Possible errors are not reported until call to jw_get().
 *
 * In:
 *      jw - ptr to json writer object
 *      val - double value
 *      name - object attribute name if writing is done inside object context;
 *             must be NULL if writing is done inside array context
 */
void jw_dbl(jwriter_t *jw, double val, const char *name)
{
    if (jw_prepv(jw, name))
        return;
    jw_printf(jw, "%f", val);
    jw->stack[jw->sidx].tt = JDBL;
}

/* Write double value with specified precision to json writer.
 * Possible errors are not reported until call to jw_get().
 *
 * In:
 *      jw - ptr to json writer object
 *      val - double value
 *      prec - precision
 *      name - object attribute name if writing is done inside object context;
 *             must be NULL if writing is done inside array context
 */
void jw_dbl_prec(jwriter_t *jw, double val, int prec, const char *name)
{
    if (jw_prepv(jw, name))
        return;
    jw_printf(jw, "%.*f", prec, val);
    jw->stack[jw->sidx].tt = JDBL;
}
#endif

/* Write string value to json writer.
 * Possible errors are not reported until call to jw_get().
 *
 * In:
 *      jw - ptr to json writer object
 *      val - string value
 *      name - object attribute name if writing is done inside object context;
 *             must be NULL if writing is done inside array context
 */
void jw_str(jwriter_t *jw, const char *str, const char *name)
{
    if (jw_prepv(jw, name))
        return;

    jw_strz(jw, "\"");
    size_t i, size = jw->len - jw->pos - 1;
    for (i = 0; i < size; i++) {
        char c = str[i];
        if (c == 0)
            break;

        // do escaping
        if (c == '"')
            jw_strz(jw, "\\\"");
        else if (c == '\\')
            jw_strz(jw, "\\\\");
        else if (c == '/')
            jw_strz(jw, "\\/");
        else if (c == '\b')
            jw_strz(jw, "\\b");
        else if (c == '\f')
            jw_strz(jw, "\\f");
        else if (c == '\n')
            jw_strz(jw, "\\n");
        else if (c == '\r')
            jw_strz(jw, "\\r");
        else if (c == '\t')
            jw_strz(jw, "\\t");
        else {
            if (jw->pos >= jw->len - 1) {
                ERROR("buffer too small");
                jw->err = 1;
                return;
            }
            jw->start[jw->pos++] = c;
        }
    }
    jw_strz(jw, "\"");

    jw->stack[jw->sidx].tt = JSTR;
}

/* Begin writing of array value to json writer.
 * Possible errors are not reported until call to jw_get().
 *
 * In:
 *      jw - ptr to json writer object
 *      name - object attribute name if writing is done inside object context;
 *             must be NULL if writing is done inside array context
 */
void jw_abegin(jwriter_t *jw, const char *name)
{
    if (jw_prepv(jw, name))
        return;
    jw_strz(jw, "[");

    if (jw->sidx >= jw->ssize - 1) {
        jw->err = 1;
        return;
    }
    jw->sidx++;

    jwstk *s = jw->stack + jw->sidx;
    s->ctx = CTXARR;
    s->tt = JASTART;
}

/* End writing of array value to json writer.
 * Possible errors are not reported until call to jw_get().
 *
 * In:
 *      jw - ptr to json writer object
 */
void jw_aend(jwriter_t *jw)
{
    if (!jw || jw->err)
        return;
    jw_strz(jw, "]");

    if (jw->sidx == 0) {
        jw->err = 1;
        return;
    }
    jw->sidx--;

    jw->stack[jw->sidx].tt = JAEND;
}

/* Begin writing of object value to json writer.
 * Possible errors are not reported until call to jw_get().
 *
 * In:
 *      jw - ptr to json writer object
 *      name - object attribute name if writing is done inside object context;
 *             must be NULL if writing is done inside array context
 */
void jw_obegin(jwriter_t *jw, const char *name)
{
    if (jw_prepv(jw, name))
        return;
    jw_strz(jw, "{");

    if (jw->sidx >= jw->ssize - 1) {
        jw->err = 1;
        return;
    }
    jw->sidx++;

    jwstk *s = jw->stack + jw->sidx;
    s->ctx = CTXOBJ;
    s->tt = JOSTART;
}

/* End writing of object value to json writer.
 * Possible errors are not reported until call to jw_get().
 *
 * In:
 *      jw - ptr to json writer object
 */
void jw_oend(jwriter_t *jw)
{
    if (!jw || jw->err)
        return;
    jw_strz(jw, "}");

    if (jw->sidx == 0) {
        jw->err = 1;
        return;
    }
    jw->sidx--;

    jw->stack[jw->sidx].tt = JOEND;
}
