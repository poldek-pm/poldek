#include "test.h"
#include "conf_intern.h"
#include "conf.h"
#include <trurl/nhash.h>

const char *expand_env_var(const char *v)
{
    char tmp[PATH_MAX];
    const char *s;
    
    s = poldek_util_expand_env_vars(tmp, sizeof(tmp), v);
    fail_if(s == NULL);
    return n_strdup(s);
}


START_TEST (test_config) {
    struct poldek_conf_tag *tags = NULL;
    tn_hash *cnf, *s;
    int i, j;
    
    cnf = poldek_conf_load("poldek_test_conf.conf", 0);
    fail_if(cnf == NULL, "load config failed");

    s = poldek_conf_get_section_ht(cnf, "global");
    fail_if(s == NULL, "no global section?");
    
    i = 0;
    while (poldek_conf_sections[i].name) {
        const char *sname = poldek_conf_sections[i].name;
        tags = poldek_conf_sections[i++].tags;

        s = poldek_conf_get_section_ht(cnf, sname);
        fail_if(s == NULL, "no %s section?", s);

        j = 0;
        while (tags[j].name) {
            struct poldek_conf_tag *t = &tags[j++];
            const char *dv, *v;

            if (strchr(t->name, '*')) /* legacy source?* and prefix*? */
                continue;

            if (t->flags & CONF_TYPE_F_ALIAS)
                continue;

            v = poldek_conf_get(s, t->name, NULL);
            fail_if(v == NULL, "%s: %s: missing?", sname, t->name);
            
            dv = t->defaultv;
            
            if (t->defaultv == NULL) { /* the xsl sets it to op name */
                char *p;
                p = n_strdup(t->name);
                dv = p;
                while (*p) {
                    if (*p == ' ') *p = '_';
                    p++;
                }
                DBGF("\n%s: %s %s\n", t->name, dv, v);
            }

            if (t->flags & CONF_TYPE_BOOLEAN) { /* reverse value, see xsl */
                if (n_str_eq(dv, "yes"))
                    dv = "no";
                else
                    dv = "yes";
            }
            
            if (t->flags & CONF_TYPE_F_ENV) {
                fail_ifnot(t->flags & CONF_TYPE_STRING);
                dv = expand_env_var(dv);
            }
            
            fail_if(n_str_ne(dv, v), "%s: %s: %s != %s",
                    sname, t->name, v, dv);
        }
    }
}
END_TEST


static char *values_list[] = { "foo", "bar", "baz", NULL };

static char *make_conf_line(const char *opname, int no, int sep) 
{
    char line[1024];
    int i = 0, n = 0;
        
    n = n_snprintf(line, sizeof(line), "%s = ", opname);
    while (values_list[i]) {
        char buf[64];
        n_snprintf(buf, sizeof(buf), "%s%d", values_list[i], no);
        n += n_snprintf(&line[n], sizeof(line) - n,
                        "%s %c", buf, sep);
        i++;
    }
    line[n - 1] = '\0';         /* remove last sep */
    return n_strdup(line);
}

static int verify_list(tn_array *list, int maxno, const char *op) 
{
    tn_hash *dict = n_hash_new(64, NULL);
    int no;

    fail_if(n_array_size(list) != 3 * maxno,
            "%s: have %d, expected %d - some values lost",
            op, n_array_size(list), 3 * maxno);
    
    for (no = maxno - 1; no >= 0; no--) { /* from max to 0, to test
                                             param overwriting
                                             (test_config_lists_excl) */
        int i = 0;
        while (values_list[i]) {
            char buf[64];
            n_snprintf(buf, sizeof(buf), "%s%d", values_list[i], no);
            n_hash_insert(dict, buf, NULL);
            i++;
        }
    }

    while (n_array_size(list) > 0) {
        const char *op = n_array_pop(list);
        fail_ifnot(n_hash_exists(dict, op), "missing list element %s", op);
    }
    return 1;
}

void make_conf_file(const char *name, tn_array *lines) 
{
    FILE *f;
    int i;
    
    f = fopen(name, "w");
    fail_if(f == NULL, "file open failed");
    for (i=0; i<n_array_size(lines); i++) 
        fprintf(f, "%s\n", n_array_nth(lines, i));
    fclose(f);
}



START_TEST (test_config_lists) {
    char *list_ops[] = { "hold", "ignore", "noproxy", "exclude_path", NULL };
    char maxno_ops[] = { 0, 0, 0, 0, 0 };
    tn_hash *cnf, *s;
    tn_array *lines, *list;
    int i, maxno = 0;
    FILE *f;
    
    lines = n_array_new(16, 0, 0);
    
    i = 0;
    while (list_ops[i]) {
        maxno = 0;
        n_array_push(lines, make_conf_line(list_ops[i], maxno++, ' '));
        n_array_push(lines, make_conf_line(list_ops[i], maxno++, ','));
        n_array_push(lines, make_conf_line(list_ops[i], maxno++, '\t'));
        if (n_str_eq(list_ops[i], "exclude_path"))
            n_array_push(lines, make_conf_line(list_ops[i], maxno++, ':'));
        if (n_str_eq(list_ops[i], "hold"))
            n_array_push(lines, make_conf_line("a hold alias for testing purposes",
                                               maxno++, ','));
        maxno_ops[i] = maxno;
        i++;
    }
    make_conf_file("poldek_test_conf.tmp", lines);
    cnf = poldek_conf_load("poldek_test_conf.tmp", 0);
    fail_if(cnf == NULL, "load config failed");

    s = poldek_conf_get_section_ht(cnf, "global");
    fail_if(s == NULL, "no global section?");

    i = 0;
    while (list_ops[i]) {
        list = poldek_conf_get_multi(s, list_ops[i]);
        fail_if(list == NULL, "no %s?", list_ops[i]);
        verify_list(list, maxno_ops[i], list_ops[i]);
        i++;
    }
}
END_TEST


START_TEST (test_config_lists_excl) {
    char *list_ops[] = { "sources", NULL };
    tn_hash *cnf, *s;
    tn_array *lines, *list;
    int i, maxno = 0;

    lines = n_array_new(16, 0, 0);
    
    i = 0;
    while (list_ops[i]) {
        maxno = 3;
        while (maxno >= 0) {
            n_array_push(lines, make_conf_line(list_ops[i], maxno, ' '));
            maxno--;
        }
        i++;
    }
    
    cnf = poldek_conf_addlines(NULL, "source", lines);
    fail_if(cnf == NULL, "load config failed");

    s = poldek_conf_get_section_ht(cnf, "source");
    fail_if(s == NULL, "no source section?");

    i = 0;
    while (list_ops[i]) {
        list = poldek_conf_get_multi(s, list_ops[i]);
        fail_if(list == NULL, "no %s?", list_ops[i]);
        verify_list(list, 1, list_ops[i]);   /* maxno = 1 */
        i++;
    }
}
END_TEST

struct test_suite test_suite_config = {
    "config", 
    {
        { "raw", test_config },
        { "lists", test_config_lists },
        
//        XXX: excl list not implemented yet
        { "lists excl", test_config_lists_excl }, 
        { NULL, NULL }
    }
};

    
        
    
        
    
        
    
        
    
        
