/**
 * @file test_branch.c
 * @brief 分支与模态栈的单测(architecture.md §6.9)。
 *
 * 验四件事:分支各持独立栈、切换不产生历史、模态叠在最上且可嵌套、
 * 模态压着时不能换分支。
 */
#include <stdio.h>
#include <string.h>

#include "lvgl_fixture.h"
#include "shrewd-lv-view/view.h"

static int g_fail;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("  FAIL %s\n", what);
        g_fail += 1;
    }
}

typedef struct {
    shrewd_lv_view_page_t base;
    int create_calls;
    int enter_calls;
    int exit_calls;
} test_page_t;

static int test_create(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params) {
    (void)params;
    test_page_t *t = (test_page_t *)p;
    t->create_calls += 1;
    p->root = lv_obj_create(container);
    return SHREWD_LV_VIEW_ERR_OK;
}

static int test_enter(shrewd_lv_view_page_t *p) {
    ((test_page_t *)p)->enter_calls += 1;
    return SHREWD_LV_VIEW_ERR_OK;
}

static void test_exit(shrewd_lv_view_page_t *p) {
    ((test_page_t *)p)->exit_calls += 1;
}

#define PAGE_DESC(nm)                                                                              \
    static const shrewd_lv_view_page_desc_t g_desc_##nm = {                                        \
        .name = #nm,                                                                               \
        .param_type = NULL,                                                                        \
        .release = SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT,                                         \
        .cache_count = 0,                                                                          \
        .create = test_create,                                                                     \
        .enter = test_enter,                                                                       \
        .exit = test_exit,                                                                         \
    }

PAGE_DESC(a);
PAGE_DESC(b);
PAGE_DESC(c);

static void page_init(test_page_t *p, const shrewd_lv_view_page_desc_t *desc) {
    memset(p, 0, sizeof(*p));
    p->base.desc = desc;
}

/* --- 1. 分支各持独立栈 ---------------------------------------------- */

static void test_branches_are_independent(void) {
    printf("test_branches_are_independent\n");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_nav_init(&nav, lv_screen_active(), lv_layer_top(), 0),
          "nav_init");

    test_page_t a;
    test_page_t b;
    test_page_t c;
    page_init(&a, &g_desc_a);
    page_init(&b, &g_desc_b);
    page_init(&c, &g_desc_c);

    /* 分支 0:两个页 */
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &a.base, NULL),
          "push a on branch 0");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &b.base, NULL),
          "push b on branch 0");
    check(2 == shrewd_lv_view_nav_depth(&nav), "branch 0 depth 2");

    /* 分支 1:一个页,互不干扰 */
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_switch_branch(&nav, 1), "switch to branch 1");
    check(0 == shrewd_lv_view_nav_depth(&nav), "branch 1 starts empty");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &c.base, NULL),
          "push c on branch 1");
    check(1 == shrewd_lv_view_nav_depth(&nav), "branch 1 depth 1");
    check(&c.base == shrewd_lv_view_nav_top(&nav), "branch 1 top is c");

    /* 切回分支 0:栈还在,不产生历史 */
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_switch_branch(&nav, 0), "back to branch 0");
    check(2 == shrewd_lv_view_nav_depth(&nav), "branch 0 stack intact");
    check(&b.base == shrewd_lv_view_nav_top(&nav), "branch 0 top is b");
    check(2 == b.enter_calls, "b re-entered without rebuild");
}

/* --- 2. 模态叠在最上,可嵌套 ---------------------------------------- */

static void test_modal_stack(void) {
    printf("test_modal_stack\n");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_nav_init(&nav, lv_screen_active(), lv_layer_top(), 0),
          "nav_init");

    test_page_t base;
    test_page_t m1;
    test_page_t m2;
    page_init(&base, &g_desc_a);
    page_init(&m1, &g_desc_b);
    page_init(&m2, &g_desc_c);

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &base.base, NULL), "push base");
    check(0 == shrewd_lv_view_nav_in_modal(&nav), "not in modal");

    /* 打开模态:push 进模态栈 */
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_modal_push(&nav, &m1.base, NULL),
          "open modal m1");
    check(1 == shrewd_lv_view_nav_in_modal(&nav), "now in modal");
    check(1 == shrewd_lv_view_nav_depth(&nav), "modal depth 1");
    check(&m1.base == shrewd_lv_view_nav_top(&nav), "top is m1");

    /* 模态里再叠一层:嵌套 */
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &m2.base, NULL), "stack modal m2");
    check(2 == shrewd_lv_view_nav_depth(&nav), "modal depth 2 (nested)");

    /* 关掉 m2:回到 m1,仍在模态里 */
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_pop(&nav), "close m2");
    check(1 == shrewd_lv_view_nav_in_modal(&nav), "still in modal after closing m2");
    check(&m1.base == shrewd_lv_view_nav_top(&nav), "top is m1 again");

    /* 关掉 m1:回到分支 */
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_pop(&nav), "close m1");
    check(0 == shrewd_lv_view_nav_in_modal(&nav), "back to branch");
    check(&base.base == shrewd_lv_view_nav_top(&nav), "top is base again");
    check(1 == shrewd_lv_view_nav_depth(&nav), "branch depth unchanged");
}

/* --- 3. 模态压着时不能换分支 ---------------------------------------- */

static void test_modal_blocks_branch_switch(void) {
    printf("test_modal_blocks_branch_switch\n");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_nav_init(&nav, lv_screen_active(), lv_layer_top(), 0),
          "nav_init");

    test_page_t base;
    test_page_t m;
    page_init(&base, &g_desc_a);
    page_init(&m, &g_desc_b);

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &base.base, NULL), "push base");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_modal_push(&nav, &m.base, NULL),
          "open modal");
    check(SHREWD_LV_VIEW_ERR_FULL == shrewd_lv_view_nav_switch_branch(&nav, 1),
          "switch_branch refused while modal is open");

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_pop(&nav), "close modal");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_switch_branch(&nav, 1),
          "switch_branch allowed after modal closes");
}

int main(void) {
    lvgl_fixture_init();
    test_branches_are_independent();
    test_modal_stack();
    test_modal_blocks_branch_switch();
    if (0 == g_fail) {
        printf("test_branch: ok\n");
        return 0;
    }
    printf("test_branch: FAILED (%d)\n", g_fail);
    return 1;
}
