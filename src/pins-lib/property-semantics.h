/*
 * property-semantics.c
 *
 *  Created on: Aug 8, 2012
 *      Author: laarman
 */

#include <ltsmin-lib/ltsmin-tl.h>
#include <pins-lib/pins.h>

extern ltsmin_expr_t parse_file(const char *file, parse_f parser, model_t model);

extern void mark_predicate(ltsmin_expr_t e, matrix_t *m); /* mark touched variables */

extern void mark_visible(ltsmin_expr_t e, matrix_t *m, int* group_visibility);  /* mark touched groups */

static inline int /* evaluate predicate on state */
eval_predicate(ltsmin_expr_t e, transition_info_t* ti, int* state)
{
    switch (e->token) {
        case PRED_TRUE:
            return 1;
        case PRED_FALSE:
            return 0;
        case PRED_NUM:
            return e->idx;
        case PRED_SVAR:
            return state[e->idx];
        case PRED_NOT:
            return !eval_predicate(e->arg1, ti, state);
        case PRED_EQ:
            return (eval_predicate(e->arg1, ti, state) == eval_predicate(e->arg2, ti, state));
        case PRED_VAR:
            if (-1 == e->num)
                Abort("unbound variable in predicate expression");
            return e->num;
        default: {
            char buf[1024];
            ltsmin_expr_print_ltl(e, buf);
            Abort("unhandled predicate expression: %s", buf);
        }
    }
    return 0;
}