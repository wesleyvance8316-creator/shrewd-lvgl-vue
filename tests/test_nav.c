/**
 * @file test_nav.c
 * @brief 导航栈的单测(architecture.md §6.6–6.7 / design.md §8)。
 *
 * 验五件事:四阶段顺序、释放策略、pop_until 的谓词语义、栈深上限、守卫四种结果。
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

/* --- 记录四阶段调用的测试页 ------------------------------------------ */

typedef struct {
    shrewd_lv_view_page_t base;
    int create_calls;
    int enter_calls;
    int exit_calls;
    int destroy_calls;
    int32_t param;
} test_page_t;

static int test_create(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params) {
    test_page_t *t = (test_page_t *)p;
    t->create_calls += 1;
    p->root = lv_obj_create(container);
    if (NULL != params) {
        t->param = *(const int32_t *)params;
    }
    return SHREWD_LV_VIEW_ERR_OK;
}

static int test_enter(shrewd_lv_view_page_t *p) {
    ((test_page_t *)p)->enter_calls += 1;
    return SHREWD_LV_VIEW_ERR_OK;
}

static void test_exit(shrewd_lv_view_page_t *p) {
    ((test_page_t *)p)->exit_calls += 1;
}

static void test_destroy(shrewd_lv_view_page_t *p) {
    ((test_page_t *)p)->destroy_calls += 1;
}

static const shrewd_basic_reflect_type_t g_i32 = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT32,
    .sb_refl_name = "int32",
    .sb_refl_size = sizeof(int32_t),
};

static const shrewd_lv_view_page_desc_t g_desc = {
    .name = "test",
    .param_type = &g_i32,
    .release = SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT,
    .cache_count = 0,
    .create = test_create,
    .enter = test_enter,
    .exit = test_exit,
    .destroy = test_destroy,
};

static const shrewd_lv_view_page_desc_t g_desc_resident = {
    .name = "test_resident",
    .param_type = &g_i32,
    .release = SHREWD_LV_VIEW_RELEASE_RESIDENT,
    .cache_count = 0,
    .create = test_create,
    .enter = test_enter,
    .exit = test_exit,
    .destroy = test_destroy,
};

static void page_init(test_page_t *p, const shrewd_lv_view_page_desc_t *desc) {
    memset(p, 0, sizeof(*p));
    p->base.desc = desc;
}

/* --- 1. 四阶段顺序 ---------------------------------------------------- */

static void test_four_stages(void) {
    printf("test_four_stages\n");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_init(&nav, lv_screen_active(), 0),
          "nav_init");

    test_page_t a;
    test_page_t b;
    page_init(&a, &g_desc);
    page_init(&b, &g_desc);

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &a.base, NULL), "push a");
    check((1 == a.create_calls) && (1 == a.enter_calls) && (0 == a.exit_calls)
              && (0 == a.destroy_calls),
          "a: create then enter");

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &b.base, NULL), "push b");
    /* DESTROY_ON_EXIT:被覆盖即失活即销毁(architecture.md §6.5)。 */
    check((1 == a.exit_calls) && (1 == a.destroy_calls), "a: exit then destroy on push(b)");
    check((1 == b.create_calls) && (1 == b.enter_calls), "b: create then enter");
    check(2 == shrewd_lv_view_nav_depth(&nav), "depth 2");

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_pop(&nav), "pop");
    check((1 == b.exit_calls) && (1 == b.destroy_calls), "b: exit then destroy (policy)");
    check(2 == a.enter_calls, "a: re-enter on pop");
    check(2 == a.create_calls, "a: rebuilt after destroy");
    check(1 == shrewd_lv_view_nav_depth(&nav), "depth 1");
}

/* --- 2. 释放策略:常驻不销毁 ------------------------------------------ */

static void test_release_policy(void) {
    printf("test_release_policy\n");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_init(&nav, lv_screen_active(), 0),
          "nav_init");

    /* a 常驻,b 离即销毁 —— 两者被覆盖时的行为应当不同。 */
    test_page_t a;
    test_page_t b;
    page_init(&a, &g_desc_resident);
    page_init(&b, &g_desc);

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &a.base, NULL), "push a");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &b.base, NULL), "push b");
    check((1 == a.exit_calls) && (0 == a.destroy_calls), "resident keeps widget when covered");
    check((1 == b.create_calls) && (1 == b.enter_calls), "b built");

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_pop(&nav), "pop");
    check((1 == b.exit_calls) && (1 == b.destroy_calls), "destroy-on-exit frees widget");
    /* 常驻:widget 还在,回退只需重新 enter,不重建。 */
    check((1 == a.create_calls) && (2 == a.enter_calls), "resident re-enters without rebuild");
}

/* --- 3. pop_until:谓词从栈顶往下找第一个 ------------------------------ */

static int pred_is_b(void *ctx, const shrewd_lv_view_page_t *page) {
    (void)ctx;
    return (0 == strcmp("test", page->desc->name)) && (page == (const shrewd_lv_view_page_t *)ctx);
}

static void test_pop_until(void) {
    printf("test_pop_until\n");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_init(&nav, lv_screen_active(), 0),
          "nav_init");

    test_page_t home;
    test_page_t mid;
    test_page_t leaf;
    page_init(&home, &g_desc);
    page_init(&mid, &g_desc);
    page_init(&leaf, &g_desc);

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &home.base, NULL), "push home");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &mid.base, NULL), "push mid");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &leaf.base, NULL), "push leaf");
    check(3 == shrewd_lv_view_nav_depth(&nav), "depth 3");

    /* 跳回 mid:leaf 被弹掉,mid 重新激活 */
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_pop_until(&nav, pred_is_b, &mid.base),
          "pop_until(mid)");
    check(2 == shrewd_lv_view_nav_depth(&nav), "depth 2 after pop_until");
    check(&mid.base == shrewd_lv_view_nav_top(&nav), "top is mid");
    check(2 == mid.enter_calls, "mid re-entered");
}

/* --- 4. 栈深上限:安全底线 -------------------------------------------- */

static void test_stack_limit(void) {
    printf("test_stack_limit\n");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_init(&nav, lv_screen_active(), 2),
          "nav_init limit=2");

    test_page_t a;
    test_page_t b;
    test_page_t c;
    page_init(&a, &g_desc);
    page_init(&b, &g_desc);
    page_init(&c, &g_desc);

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &a.base, NULL), "push 1");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &b.base, NULL), "push 2");
    check(SHREWD_LV_VIEW_ERR_NAV_STACK_FULL == shrewd_lv_view_nav_push(&nav, &c.base, NULL),
          "push 3 refused: over limit");
}

/* --- 5. 守卫:四种结果 ------------------------------------------------ */

static shrewd_lv_view_guard_result_t g_guard_result;
static int g_guard_calls;
static shrewd_lv_view_page_t *g_guard_redirect;

static shrewd_lv_view_guard_result_t test_guard(void *ctx, shrewd_lv_view_nav_request_t *req) {
    (void)ctx;
    (void)req;
    g_guard_calls += 1;
    if (SHREWD_LV_VIEW_GUARD_REDIRECT == g_guard_result) {
        req->redirect = g_guard_redirect;
    }
    return g_guard_result;
}

static void test_guard_results(void) {
    printf("test_guard_results\n");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_init(&nav, lv_screen_active(), 0),
          "nav_init");
    shrewd_lv_view_nav_set_guard(&nav, test_guard, NULL);

    test_page_t a;
    test_page_t b;
    test_page_t c;
    page_init(&a, &g_desc);
    page_init(&b, &g_desc);
    page_init(&c, &g_desc);

    /* ALLOW */
    g_guard_result = SHREWD_LV_VIEW_GUARD_ALLOW;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &a.base, NULL), "push allowed");

    /* CANCEL:留在原页 */
    g_guard_result = SHREWD_LV_VIEW_GUARD_CANCEL;
    check(SHREWD_LV_VIEW_ERR_NAV_CANCELLED == shrewd_lv_view_nav_push(&nav, &b.base, NULL),
          "push cancelled");
    check(1 == shrewd_lv_view_nav_depth(&nav), "depth unchanged after cancel");
    check(&a.base == shrewd_lv_view_nav_top(&nav), "still on a");

    /* CONSUME:请求被吃掉,不真走 */
    g_guard_result = SHREWD_LV_VIEW_GUARD_CONSUME;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &b.base, NULL), "push consumed");
    check(1 == shrewd_lv_view_nav_depth(&nav), "depth unchanged after consume");
    check(&a.base == shrewd_lv_view_nav_top(&nav), "still on a after consume");

    /* REDIRECT:改道到 c */
    g_guard_result = SHREWD_LV_VIEW_GUARD_REDIRECT;
    g_guard_redirect = &c.base;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &b.base, NULL), "push redirected");
    check(&c.base == shrewd_lv_view_nav_top(&nav), "redirected to c");
}

int main(void) {
    lvgl_fixture_init();
    test_four_stages();
    test_release_policy();
    test_pop_until();
    test_stack_limit();
    test_guard_results();
    if (0 == g_fail) {
        printf("test_nav: ok\n");
        return 0;
    }
    printf("test_nav: FAILED (%d)\n", g_fail);
    return 1;
}
