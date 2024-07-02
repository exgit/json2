#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*****************************************************************************
* Helper functions.
*****************************************************************************/

static bool is_node_int(jnode_t* n, int val)
{
    if (n->type != JT_INT)
        return false;
    return (n->int_val == val);
}

#if JSON_DOUBLE == 1
static bool is_node_dbl(jnode_t* n, double val)
{
    double eps = 0.000001;
    if (n->type != JT_DBL)
        return false;
    return (val - eps <= n->dbl_val && n->dbl_val <= val + eps);
}
#endif

static bool is_node_str(jnode_t* n, const char* val)
{
    if (n->type != JT_STR)
        return false;
    return (0 == strcmp(n->str_val, val));
}


/*****************************************************************************
* Common data.
*****************************************************************************/

static char* json;
static size_t jsize;
static jnode_t* node;
static jwriter_t* jw;
static jparser_t* jp;


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
    jnode_t* n;

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
    const char* val = "Test String!";

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
    const char* val3 = "Test String 57589347";
    jnode_t* n;

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
    const char* names[count] = { "obj_111", "obj_222", "obj_333" };
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
        jnode_t* n = jn_elt(node, i);
        if (n->type != JT_OBJ)
            return false;
        if (!is_node_int(jn_attr(n, "id"), ids[i]))
            return false;
        if (!is_node_str(jn_attr(n, "name"), names[i]))
            return false;
    }

    return true;
}


/*****************************************************************************
* List of all test functions.
*****************************************************************************/

typedef bool (*test_f)(void);
static test_f tests[] = {
    Test1,
    Test2,
    Test3,
    Test4,
    Test5,
    Test6
};


/*****************************************************************************
* Entry point.
*****************************************************************************/

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    int res = jw_create(&jw, 0, 0);
    if (res) {
        printf("jw_create() error.");
        return 1;
    }

    res = jp_create(&jp, 0, 0);
    if (res) {
        printf("jp_create() error.");
        return 1;
    }

    // number of tests
    size_t tcount = sizeof(tests) / sizeof(tests[0]);

    // array for results
    bool* results = malloc(tcount * sizeof(bool));
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

    return 0;
}
