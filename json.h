#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Define JSON_DOUBLE as 1, if your json data contains floating point numbers
 * and your platform supports 'double' type.
 */
#define JSON_DOUBLE 1

// Json value types.
typedef enum _jtype_t {
    JT_NONE, // absent value
    JT_NULL, // null
    JT_BOOL, // boolean
    JT_INT, // integer
#if JSON_DOUBLE == 1
    JT_DBL, // double
#endif
    JT_STR, // string (zero terminated)
    JT_ARR, // array
    JT_OBJ // object
} jtype_t;

// Json node represent json value after parsing.
typedef struct _jnode_t jnode_t;
struct _jnode_t {
    jtype_t type; // json value type

    union {
        bool bool_val; // boolean value
        int int_val; // int value
#if JSON_DOUBLE == 1
        double dbl_val; // double value
#endif

        struct {
            const char *str_val; // string (zero terminated)
            int str_len; // string length
        };

        // array elements
        struct {
            jnode_t **values; // array of element values
            int count; // element count
        } elts;

        // object attributes
        struct {
            const char **names; // array of attribute names
            jnode_t **values; // array of attribute values
            int count; // attribute count
        } attrs;
    };
};

// Json parser opaque object.
typedef struct _jparser_t jparser_t;

// Json writer opaque object.
typedef struct _jwriter_t jwriter_t;

// Json node methods.
jnode_t *jn_elt(jnode_t *node, int i);
jnode_t *jn_attr(jnode_t *node, const char *name);

// Json parser methods.
int jp_create(jparser_t **jp, size_t mem, size_t stack);
void jp_destroy(jparser_t *jp);
int jp_parse(jparser_t *jp, jnode_t **root, const char *str, size_t len);

// Json writer methods.
int jw_create(jwriter_t **jw, size_t mem, size_t stack);
void jw_destroy(jwriter_t *jw);

void jw_pretty_print(jwriter_t *jw, int depth, int margin);

void jw_begin(jwriter_t *jw);
int jw_get(jwriter_t *jw, char **str, size_t *size);

void jw_null(jwriter_t *jw, const char *name);
void jw_bool(jwriter_t *jw, bool val, const char *name);
#if JSON_DOUBLE == 1
void jw_dbl(jwriter_t *jw, double val, const char *name);
void jw_dbl_prec(jwriter_t *jw, double val, int prec, const char *name);
#endif
void jw_int(jwriter_t *jw, int val, const char *name);
void jw_str(jwriter_t *jw, const char *str, const char *name);

void jw_abegin(jwriter_t *jw, const char *name);
void jw_aend(jwriter_t *jw);

void jw_obegin(jwriter_t *jw, const char *name);
void jw_oend(jwriter_t *jw);
