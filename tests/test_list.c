/**
 * @file test_list.c
 * @brief 列表对账的单测(design.md §10)。
 *
 * 验五件事:增删复用(`build` 只跑一次)、顺序变化不重建、条件渲染退化形态、
 * 容器删除级联自清、虚拟化只物化可视窗口。
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

/* --- 一个极简数据源:连续的 int32,key 就是值本身 ------------------ */

static int32_t g_items[8];
static uint32_t g_count;
static int g_build_calls;
static int g_teardown_calls;

static const void *src_at(void *ctx, uint32_t i) {
    (void)ctx;
    return &g_items[i];
}

static uint32_t src_key(void *ctx, uint32_t i) {
    (void)ctx;
    return (uint32_t)g_items[i];
}

static lv_obj_t *src_build(void *ctx, lv_obj_t *container, const void *item, void *instance) {
    (void)ctx;
    (void)item;
    (void)instance;
    g_build_calls += 1;
    lv_obj_t *child = lv_obj_create(container);
    lv_obj_set_size(child, 40, 10);
    return child;
}

static void src_teardown(void *ctx, void *instance) {
    (void)ctx;
    (void)instance;
    g_teardown_calls += 1;
}

static const shrewd_lv_view_list_desc_t g_desc = {
    .at = src_at,
    .key = src_key,
    .item_size = sizeof(int32_t),
    .instance_size = 0,
    .build = src_build,
    .teardown = src_teardown,
};

static void set_list(shrewd_basic_reflect_ref_t *ref,
                     shrewd_lv_view_list_t *slot,
                     const int32_t *values,
                     uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        g_items[i] = values[i];
    }
    g_count = n;
    slot->items = g_items;
    slot->count = n;
    (void)shrewd_basic_reflect_ref_set(ref, slot, sizeof(*slot));
}

static void reset_counters(void) {
    g_build_calls = 0;
    g_teardown_calls = 0;
}

/* --- 1. 增删复用:build 只跑一次 ------------------------------------ */

static void test_add_remove_reuse(void) {
    printf("test_add_remove_reuse\n");
    lv_obj_t *container = lv_obj_create(lv_screen_active());

    shrewd_lv_view_list_t slot;
    memset(&slot, 0, sizeof(slot));
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, &slot, sizeof(slot)),
          "ref_init");
    reset_counters();
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bind_list(container, &ref, &g_desc, NULL),
          "bind_list");

    static const int32_t first[3] = {10, 20, 30};
    set_list(&ref, &slot, first, 3);
    check(3 == g_build_calls, "3 built");
    check(0 == g_teardown_calls, "none torn down");
    check(3 == lv_obj_get_child_count(container), "3 children");

    /* 10、30 复用;20 删掉;40 新建 */
    static const int32_t second[3] = {10, 30, 40};
    set_list(&ref, &slot, second, 3);
    check(4 == g_build_calls, "only 1 rebuilt (40)");
    check(1 == g_teardown_calls, "1 torn down (20)");
    check(3 == lv_obj_get_child_count(container), "still 3 children");
}

/* --- 2. 顺序变化:挪动,不重建 -------------------------------------- */

static void test_reorder(void) {
    printf("test_reorder\n");
    lv_obj_t *container = lv_obj_create(lv_screen_active());

    shrewd_lv_view_list_t slot;
    memset(&slot, 0, sizeof(slot));
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, &slot, sizeof(slot)),
          "ref_init");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bind_list(container, &ref, &g_desc, NULL),
          "bind_list");

    static const int32_t first[3] = {10, 20, 30};
    reset_counters();
    set_list(&ref, &slot, first, 3);
    check(3 == g_build_calls, "3 built");

    /* 同一批 key,换个次序:不许重建 */
    static const int32_t reordered[3] = {30, 10, 20};
    set_list(&ref, &slot, reordered, 3);
    check(3 == g_build_calls, "no rebuild on reorder");
    check(0 == g_teardown_calls, "no teardown on reorder");
    check(3 == lv_obj_get_child_count(container), "still 3 children");
}

/* --- 3. 条件渲染:key 集合 0 或 1(§10 退化形态)-------------------- */

static void test_conditional(void) {
    printf("test_conditional\n");
    lv_obj_t *container = lv_obj_create(lv_screen_active());

    shrewd_lv_view_list_t slot;
    memset(&slot, 0, sizeof(slot));
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, &slot, sizeof(slot)),
          "ref_init");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bind_list(container, &ref, &g_desc, NULL),
          "bind_list");
    reset_counters();

    static const int32_t one[1] = {7};
    set_list(&ref, &slot, one, 1);
    check(1 == g_build_calls, "one child built");
    check(1 == lv_obj_get_child_count(container), "one child");

    static const int32_t none[1] = {0};
    set_list(&ref, &slot, none, 0);
    check(1 == g_teardown_calls, "child torn down");
    check(0 == lv_obj_get_child_count(container), "no children");
}

/* --- 4. 子项实例由框架分配(design.md §14.1 摩擦点 1)---------------- */

typedef struct {
    lv_obj_t *root;
    int marker;
} inst_t;

static lv_obj_t *inst_build(void *ctx, lv_obj_t *container, const void *item, void *instance) {
    (void)ctx;
    (void)item;
    inst_t *t = (inst_t *)instance;
    if (NULL == t) {
        return NULL; /* 框架没给实例 —— 立刻暴露 */
    }
    t->marker = 4242;
    t->root = lv_obj_create(container);
    lv_obj_set_user_data(t->root, t);
    return t->root;
}

static void inst_teardown(void *ctx, void *instance) {
    (void)ctx;
    inst_t *t = (inst_t *)instance;
    if ((NULL != t) && (4242 == t->marker)) {
        g_teardown_calls += 1; /* 收到的确实是自己的实例 */
    }
}

static const shrewd_lv_view_list_desc_t inst_desc = {
    .at = src_at,
    .key = src_key,
    .item_size = sizeof(int32_t),
    .instance_size = sizeof(inst_t),
    .build = inst_build,
    .teardown = inst_teardown,
};

static void test_instance_owned(void) {
    printf("test_instance_owned\n");
    lv_obj_t *container = lv_obj_create(lv_screen_active());

    shrewd_lv_view_list_t slot;
    memset(&slot, 0, sizeof(slot));
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, &slot, sizeof(slot)),
          "ref_init");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bind_list(container, &ref, &inst_desc, NULL),
          "bind_list");
    reset_counters();

    static const int32_t two[2] = {1, 2};
    set_list(&ref, &slot, two, 2);
    check(2 == lv_obj_get_child_count(container), "2 children with instances");

    /* 收尾:框架应把实例交给 teardown 再释放。 */
    static const int32_t none[1] = {0};
    set_list(&ref, &slot, none, 0);
    check(2 == g_teardown_calls, "teardown got its own instance for both");
}

/* --- 5. 容器删除级联自清 -------------------------------------------- */

static void test_container_delete(void) {
    printf("test_container_delete\n");
    lv_obj_t *container = lv_obj_create(lv_screen_active());

    shrewd_lv_view_list_t slot;
    memset(&slot, 0, sizeof(slot));
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, &slot, sizeof(slot)),
          "ref_init");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bind_list(container, &ref, &g_desc, NULL),
          "bind_list");

    static const int32_t two[2] = {1, 2};
    set_list(&ref, &slot, two, 2);
    lv_obj_delete(container);

    /* 绑定应已自清:再改源不得触碰已删除的容器。 */
    static const int32_t three[3] = {1, 2, 3};
    set_list(&ref, &slot, three, 3);
}

/* --- 6. 虚拟化:item_height > 0 → 只物化可视窗口 -------------------- */

static int32_t g_big[200];
static uint32_t g_big_count;
static int g_big_builds;

static const void *big_at(void *ctx, uint32_t i) {
    (void)ctx;
    return &g_big[i];
}

static uint32_t big_key(void *ctx, uint32_t i) {
    (void)ctx;
    return (uint32_t)g_big[i];
}

static lv_obj_t *big_build(void *ctx, lv_obj_t *container, const void *item, void *instance) {
    (void)ctx;
    (void)item;
    (void)instance;
    g_big_builds += 1;
    return lv_obj_create(container);
}

static const shrewd_lv_view_list_desc_t big_desc = {
    .at = big_at,
    .key = big_key,
    .item_size = sizeof(int32_t),
    .instance_size = 0,
    .item_height = 20,
    .build = big_build,
};

static void big_set(uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        g_big[i] = (int32_t)(1000 + i); /* key 唯一且稳定 */
    }
    g_big_count = n;
}

static void test_virtualization(void) {
    printf("test_virtualization\n");
    lv_obj_t *container = lv_obj_create(lv_screen_active());
    lv_obj_set_size(container, 200, 100); /* 可视 5 行 */

    shrewd_lv_view_list_t slot;
    memset(&slot, 0, sizeof(slot));
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, &slot, sizeof(slot)),
          "ref_init");

    big_set(100);
    g_big_builds = 0;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bind_list(container, &ref, &big_desc, NULL),
          "bind_list (virtual)");

    slot.items = g_big;
    slot.count = g_big_count;
    (void)shrewd_basic_reflect_ref_set(&ref, &slot, sizeof(slot));

    /* 100 项,只该物化窗口那几个(±1 根撑高度的 spacer) */
    uint32_t kids = lv_obj_get_child_count(container);
    check(kids < 20, "100 items do NOT make 100 widgets");
    check(3 < kids, "window is not empty");
    check((int)kids - 1 == g_big_builds, "one build per materialized row (minus spacer)");

    /* 滚动 → 窗口滑动 → 边缘上补新的;实例数不变 */
    uint32_t builds_before = (uint32_t)g_big_builds;
    lv_obj_scroll_by_bounded(container, 0, -200, LV_ANIM_OFF); /* 滚 10 行 */
    check(builds_before < (uint32_t)g_big_builds, "scrolling materializes new rows");
    /* 窗口大小不保证恒定(顶/底边缘处本来就短),但必须始终有界 */
    uint32_t scrolled = lv_obj_get_child_count(container);
    check(scrolled < 20, "window stays bounded while scrolling");

    /* 结构变化:砍掉一半 → 窗口重算,不得留悬垂 */
    big_set(50);
    slot.items = g_big;
    slot.count = g_big_count;
    (void)shrewd_basic_reflect_ref_set(&ref, &slot, sizeof(slot));
    check(lv_obj_get_child_count(container) <= scrolled, "window still bounded after a shrink");
}

int main(void) {
    lvgl_fixture_init();
    test_add_remove_reuse();
    test_reorder();
    test_conditional();
    test_instance_owned();
    test_container_delete();
    test_virtualization();
    if (0 == g_fail) {
        printf("test_list: ok\n");
        return 0;
    }
    printf("test_list: FAILED (%d)\n", g_fail);
    return 1;
}
