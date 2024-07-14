#include "../json.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


/*****************************************************************************
* Helper functions.
*****************************************************************************/

static bool is_node_int(jnode_t *n, int val)
{
    if (n->type != JT_INT)
        return false;
    return (n->int_val == val);
}

#if JSON_DOUBLE == 1
static bool is_node_dbl(jnode_t *n, double val)
{
    double eps = 0.000001;
    if (n->type != JT_DBL)
        return false;
    return (val - eps <= n->dbl_val && n->dbl_val <= val + eps);
}
#endif

static bool is_node_str(jnode_t *n, const char *val)
{
    if (n->type != JT_STR)
        return false;
    return (0 == strcmp(n->str_val, val));
}

static void *read_file_to_mem(const char *filename, size_t *outSize)
{
    char filename_buf[256];
    snprintf(filename_buf, sizeof(filename_buf), "%s/%s", SRCDIR, filename);

    FILE *file = fopen(filename_buf, "rb");
    if (!file)
        return NULL;

    fseek(file, 0, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buf = malloc(size + 1);
    size_t items = fread(buf, size, 1, file);
    if (items != 1) {
        free(buf);
        buf = NULL;
        *outSize = 0;
    } else {
        buf[size] = 0;
        *outSize = size;
    }
    fclose(file);

    return buf;
}


/*****************************************************************************
* Timer functions.
*****************************************************************************/

#define MYTIMER_STATE_CREATED   1
#define MYTIMER_STATE_STARTED   2
#define MYTIMER_STATE_STOPPED   3

typedef struct mytimer {
    int state;
    struct timespec ts;
    char result[64];
} mytimer_t;

mytimer_t *mytimer_create()
{
    mytimer_t *timer;
    timer = (mytimer_t*)malloc(sizeof(mytimer_t));
    timer->state = MYTIMER_STATE_CREATED;
    snprintf(timer->result, sizeof(timer->result), "NULL");
    return timer;
}

void mytimer_destroy(mytimer_t *timer)
{
    free(timer);
}

void mytimer_start(mytimer_t *timer)
{
    clock_gettime(CLOCK_MONOTONIC, &timer->ts);
    timer->state = MYTIMER_STATE_STARTED;
    snprintf(timer->result, sizeof(timer->result), "NULL");
}

void mytimer_stop(mytimer_t *timer)
{
    if (timer->state != MYTIMER_STATE_STARTED)
        return;
    struct timespec ts = timer->ts;
    clock_gettime(CLOCK_MONOTONIC, &timer->ts);
    timer->ts.tv_sec -= ts.tv_sec;
    timer->ts.tv_nsec -= ts.tv_nsec;
    if (timer->ts.tv_nsec < 0) {
        timer->ts.tv_sec--;
        timer->ts.tv_nsec += 1000000000;
    }
    timer->state = MYTIMER_STATE_STOPPED;
    snprintf(timer->result, sizeof(timer->result),
             "Elapsed time: %d.%03d sec",
             (int)timer->ts.tv_sec,
             (int)(timer->ts.tv_nsec / 1000000));
}


/*****************************************************************************
* Common data.
*****************************************************************************/

static char *json;
static size_t jsize;
static jnode_t *node;
static jwriter_t *jw;
static jparser_t *jp;


/*****************************************************************************
* Test functions.
*****************************************************************************/

// Single int value.
static bool Test1(void)
{
    jw_begin(jw);
    {
        jw_int(jw, 55, NULL);
    }
    if (jw_get(jw, &json, &jsize))
        return false;

    printf("%s: %s\n", __func__, json);

    if (jp_parse(jp, &node, json, jsize))
        return false;
    return is_node_int(node, 55);
}


// Single double value.
static bool Test2(void)
{
#if JSON_DOUBLE == 1
    double val = 3.14159265358979323846;
    jnode_t *n;

    jw_begin(jw);
    {
        jw_abegin(jw, NULL);
        {
            jw_dbl(jw, val, NULL);
            jw_dbl_prec(jw, val, 20, NULL);
        }
        jw_aend(jw);
    }
    if (jw_get(jw, &json, &jsize))
        return false;

    printf("%s: %s\n", __func__, json);

    if (jp_parse(jp, &node, json, jsize))
        return false;
    if (node->type != JT_ARR)
        return false;
    n = jn_elt(node, 0);
    if (!is_node_dbl(n, val))
        return false;
    n = jn_elt(node, 1);
    if (!is_node_dbl(n, val))
        return false;
#endif

    return true;
}


// Single string value.
static bool Test3(void)
{
    const char *val = "Hello world! Здравствуй мир! שלום עולם! 你好世界！";

    jw_begin(jw);
    {
        jw_str(jw, val, NULL);
    }
    if (jw_get(jw, &json, &jsize))
        return false;

    printf("%s: %s\n", __func__, json);

    if (jp_parse(jp, &node, json, jsize))
        return false;
    return is_node_str(node, val);
}


// Array of 3 elements.
static bool Test4(void)
{
    int val1 = 223344;
    int val2 = 867757;
    const char *val3 = "Test String '1234567'";
    jnode_t *n;

    jw_begin(jw);
    {
        jw_abegin(jw, NULL);
        {
            jw_int(jw, val1, NULL);
            jw_int(jw, val2, NULL);
            jw_str(jw, val3, NULL);
        }
        jw_aend(jw);
    }
    if (jw_get(jw, &json, &jsize))
        return false;

    printf("%s: %s\n", __func__, json);

    if (jp_parse(jp, &node, json, jsize))
        return false;
    if (node->type != JT_ARR)
        return false;
    n = jn_elt(node, 0);
    if (!is_node_int(n, val1))
        return false;
    n = jn_elt(node, 1);
    if (!is_node_int(n, val2))
        return false;
    n = jn_elt(node, 2);
    if (!is_node_str(n, val3))
        return false;
    return true;
}


// Object with 9 attributes.
static bool Test5(void)
{
    jw_begin(jw);
    {
        jw_obegin(jw, NULL);
        {
            jw_int(jw, 800, "abc1");
            jw_int(jw, 801, "def1");
            jw_int(jw, 802, "ghi1");
            jw_int(jw, 803, "abc2");
            jw_int(jw, 804, "def2");
            jw_int(jw, 805, "ghi2");
            jw_int(jw, 806, "abc3");
            jw_int(jw, 807, "def3");
            jw_int(jw, 808, "ghi3");
        }
        jw_oend(jw);
    }
    if (jw_get(jw, &json, &jsize))
        return false;

    printf("%s: %s\n", __func__, json);

    if (jp_parse(jp, &node, json, jsize))
        return false;
    if (node->type != JT_OBJ)
        return false;
    if (!is_node_int(jn_attr(node, "abc1"), 800))
        return false;
    if (!is_node_int(jn_attr(node, "def1"), 801))
        return false;
    if (!is_node_int(jn_attr(node, "ghi1"), 802))
        return false;
    if (!is_node_int(jn_attr(node, "abc2"), 803))
        return false;
    if (!is_node_int(jn_attr(node, "def2"), 804))
        return false;
    if (!is_node_int(jn_attr(node, "ghi2"), 805))
        return false;
    if (!is_node_int(jn_attr(node, "abc3"), 806))
        return false;
    if (!is_node_int(jn_attr(node, "def3"), 807))
        return false;
    if (!is_node_int(jn_attr(node, "ghi3"), 808))
        return false;

    return true;
}


// Array of objects.
static bool Test6(void)
{
    enum { count = 3 };
    int ids[count] = { 111, 222, 333 };
    const char *names[count] = { "obj_111", "obj_222", "obj_333" };
    int i;

    jw_begin(jw);
    {
        jw_abegin(jw, NULL);
        for (i = 0; i < count; i++) {
            jw_obegin(jw, NULL);
            {
                jw_int(jw, ids[i], "id");
                jw_str(jw, names[i], "name");
            }
            jw_oend(jw);
        }
        jw_aend(jw);
    }
    if (jw_get(jw, &json, &jsize))
        return false;

    printf("%s: %s\n", __func__, json);

    if (jp_parse(jp, &node, json, jsize))
        return false;
    if (node->type != JT_ARR)
        return false;
    if (node->elts.count != 3)
        return false;
    for (i = 0; i < count; i++) {
        jnode_t *n = jn_elt(node, i);
        if (n->type != JT_OBJ)
            return false;
        if (!is_node_int(jn_attr(n, "id"), ids[i]))
            return false;
        if (!is_node_str(jn_attr(n, "name"), names[i]))
            return false;
    }

    return true;
}

static bool Test7(void)
{
    bool ret = false;
    char *fj1=NULL, *fj2=NULL, *fj3=NULL;
    size_t fj1size=0, fj2size=0, fj3size=0;
    const char *fn1 = "data/canada.json";
    const char *fn2 = "data/citm_catalog.json";
    const char *fn3 = "data/twitter.json";

    const char *filename = fn1;
    fj1 = read_file_to_mem(filename, &fj1size);
    if (!fj1) goto erropen;
    printf("%s: Loaded file '%s'.\n", __func__, filename);

    filename = fn2;
    fj2 = read_file_to_mem(filename, &fj2size);
    if (!fj2) goto erropen;
    printf("%s: Loaded file '%s'.\n", __func__, filename);

    filename = fn3;
    fj3 = read_file_to_mem(filename, &fj3size);
    if (!fj3) goto erropen;
    printf("%s: Loaded file '%s'.\n", __func__, filename);

    mytimer_t *timer = mytimer_create();
    mytimer_start(timer);
    if (jp_parse(jp, &node, fj1, fj1size)) {
        printf("%s: Failed to parse file '%s'.\n", __func__, fn1);
        goto exit;
    }
    if (jp_parse(jp, &node, fj2, fj2size)) {
        printf("%s: Failed to parse file '%s'.\n", __func__, fn2);
        goto exit;
    }
    if (jp_parse(jp, &node, fj3, fj3size)) {
        printf("%s: Failed to parse file '%s'.\n", __func__, fn3);
        goto exit;
    }
    mytimer_stop(timer);
    printf("%s: %s.\n", __func__, timer->result);
    mytimer_destroy(timer);

    ret = true;
    goto exit;

erropen:
    printf("%s: Failed to open file '%s'. Error %d - %s.\n",
           __func__, filename, errno, strerror(errno));

exit:
    free(fj1);
    free(fj2);
    free(fj3);
    return ret;
}

static bool Test8(void)
{
    json =
"{\n"
"  // single-line comment\n"
"  attr1: 1,\n"
"\n"
"  /*\n"
"   * multi-line comment\n"
"   */\n"
"  attr2: 2\n"
"}";

    printf("%s: %s\n", __func__, json);

    if (jp_parse(jp, &node, json, strlen(json)))
        return false;
    if (node->type != JT_OBJ)
        return false;
    if (!is_node_int(jn_attr(node, "attr1"), 1))
        return false;
    if (!is_node_int(jn_attr(node, "attr2"), 2))
        return false;

    return true;
}


/*****************************************************************************
* List of all test functions.
*****************************************************************************/

typedef bool (*test_f)(void);
static test_f tests[] = {
    Test1, Test2, Test3, Test4,
    Test5, Test6, Test7, Test8
};


/*****************************************************************************
* Entry point.
*****************************************************************************/

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    int res = jw_create(&jw, 0, 0);
    if (res) {
        printf("jw_create() error.");
        return 1;
    }
    jw_pretty_print(jw, 2, 2);

    res = jp_create(&jp, 0, 0);
    if (res) {
        printf("jp_create() error.");
        return 1;
    }

    // number of tests
    size_t tcount = sizeof(tests) / sizeof(tests[0]);

    // array for results
    bool *results = malloc(tcount * sizeof(bool));
    memset(results, 0, tcount * sizeof(bool));

    // run tests
    for (int i = 0; i < (int)tcount; i++) {
        results[i] = (tests[i])();
    }

    // print results
    printf("\n");
    for (int i = 0; i < (int)tcount; i++) {
        printf("Test %d %s\r\n", i + 1, (results[i] ? "  Ok" : "Fail"));
    }

    free(results);
    return 0;
}
