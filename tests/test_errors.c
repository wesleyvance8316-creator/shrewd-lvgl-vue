/**
 * @file test_errors.c
 * @brief 错误码跨仓约定的单测(design.md §12)。
 *
 * 验四件事:本仓的码由本仓命名、shrewd-basic 的失败**原样透传不跨层重编码**、
 * `error_name` 对不认识的码回落 `shrewd_basic_error_name`(不丢信息)、本仓自己的
 * 检查仍返回本仓的码。
 *
 * 透传那条是真跑出来的:塞一个宽度为 SIZE_MAX 的 param_type,让
 * `shrewd_basic_platform_alloc` 真的失败,再看返回的是**它的**码(-1),
 * 而不是本仓的 MEM_NOMEM(-0x6002)。谁把翻译加回来,这条就红。
 */
#include <stdio.h>
#include <string.h>

#include "lvgl_fixture.h"
#include "shrewd-basic/platform/error.h"
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
} test_page_t;

static int test_create(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params) {
    (void)params;
    p->root = lv_obj_create(container);
    return SHREWD_LV_VIEW_ERR_OK;
}

/* --- 1. 本仓的码由本仓命名 ------------------------------------------ */

static void test_own_names(void) {
    printf("test_own_names\n");
    check(0 == strcmp("SHREWD_LV_VIEW_ERR_OK", shrewd_lv_view_error_name(SHREWD_LV_VIEW_ERR_OK)),
          "OK named");
    check(0
              == strcmp("SHREWD_LV_VIEW_ERR_ARG_INVALID",
                        shrewd_lv_view_error_name(SHREWD_LV_VIEW_ERR_ARG_INVALID)),
          "ARG_INVALID named");
    check(0
              == strcmp("SHREWD_LV_VIEW_ERR_NAV_EMPTY",
                        shrewd_lv_view_error_name(SHREWD_LV_VIEW_ERR_NAV_EMPTY)),
          "NAV_EMPTY named");
}

/* --- 2. 不认识的码回落 shrewd-basic(不丢信息)------------------------ */

static void test_fallback_name(void) {
    printf("test_fallback_name\n");
    /* shrewd-basic 的码不是本仓的,不该被改名,更不该变成 UNKNOWN。 */
    check(0
              == strcmp(shrewd_basic_error_name(SHREWD_BASIC_ERR_MEM_NOMEM),
                        shrewd_lv_view_error_name(SHREWD_BASIC_ERR_MEM_NOMEM)),
          "basic codes fall back to basic names");
    check(0
              == strcmp("SHREWD_BASIC_ERR_MEM_NOMEM",
                        shrewd_lv_view_error_name(SHREWD_BASIC_ERR_MEM_NOMEM)),
          "…and the name is the basic one");
    check(0
              == strcmp(shrewd_basic_error_name(SHREWD_BASIC_ERR_DBUS_CONFLICT),
                        shrewd_lv_view_error_name(SHREWD_BASIC_ERR_DBUS_CONFLICT)),
          "domain codes survive too");
}

/* --- 3. 透传:上游失败不重编码 -------------------------------------- */

/** 宽度 SIZE_MAX 的假 param_type —— 只为了让 params_copy 里的分配真的失败。 */
static const shrewd_basic_reflect_type_t g_huge = {
    .sb_refl_kind = SHREWD_BASIC_REFLECT_KIND_INT32,
    .sb_refl_name = "huge",
    .sb_refl_size = (size_t)-1,
};

static const shrewd_lv_view_page_desc_t g_desc_huge = {
    .name = "huge",
    .param_type = &g_huge,
    .release = SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT,
    .create = test_create,
};

static void test_passthrough(void) {
    printf("test_passthrough\n");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_nav_init(&nav, lv_screen_active(), lv_layer_top(), 0),
          "nav_init");

    test_page_t page;
    memset(&page, 0, sizeof(page));
    page.base.desc = &g_desc_huge;

    int32_t params = 0;
    int status = shrewd_lv_view_nav_push(&nav, &page.base, &params);
    /* 上游 alloc 失败是 -1(SHREWD_BASIC_ERR_MEM_NOMEM),不是 -0x6002。 */
    check(SHREWD_BASIC_ERR_MEM_NOMEM == status, "shrewd-basic failure passed through unmapped");
    check(SHREWD_LV_VIEW_ERR_MEM_NOMEM != status, "not re-encoded into this repo's code");
    check(0 == strcmp("SHREWD_BASIC_ERR_MEM_NOMEM", shrewd_lv_view_error_name(status)),
          "error_name still names it as the basic code");
    check(0 == shrewd_lv_view_nav_depth(&nav), "nothing pushed on failure");
}

/* --- 4. 本仓自己的检查仍用本仓的码 ---------------------------------- */

static void test_own_codes(void) {
    printf("test_own_codes\n");
    check(SHREWD_LV_VIEW_ERR_ARG_INVALID == shrewd_lv_view_nav_push(NULL, NULL, NULL),
          "own arg check keeps its own code");
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_nav_init(&nav, lv_screen_active(), lv_layer_top(), 0),
          "nav_init");
    check(SHREWD_LV_VIEW_ERR_NAV_EMPTY == shrewd_lv_view_nav_pop(&nav), "own state code");
}

int main(void) {
    lvgl_fixture_init();
    test_own_names();
    test_fallback_name();
    test_passthrough();
    test_own_codes();
    if (0 == g_fail) {
        printf("test_errors: ok\n");
        return 0;
    }
    printf("test_errors: FAILED (%d)\n", g_fail);
    return 1;
}
