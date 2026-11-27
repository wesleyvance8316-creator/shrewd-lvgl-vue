/**
 * @file test_shell.c
 * @brief 壳的单测(architecture.md §6.9)。
 *
 * 验三件事:壳的三段结构、nav->host 指到内容区、**标题跟着导航自动变**
 * (壳的 UI 是导航状态的绑定,不是手动同步)。
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
} test_page_t;

static int test_create(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params) {
    (void)params;
    p->root = lv_obj_create(container);
    return SHREWD_LV_VIEW_ERR_OK;
}

#define PAGE_DESC(nm)                                                                              \
    static const shrewd_lv_view_page_desc_t g_desc_##nm = {                                        \
        .name = #nm,                                                                               \
        .param_type = NULL,                                                                        \
        .release = SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT,                                         \
        .create = test_create,                                                                     \
    }

PAGE_DESC(home);
PAGE_DESC(settings);

static void page_init(test_page_t *p, const shrewd_lv_view_page_desc_t *desc) {
    memset(p, 0, sizeof(*p));
    p->base.desc = desc;
}

static void test_shell(void) {
    printf("test_shell\n");
    lv_display_t *disp = lv_display_get_default();
    check(NULL != disp, "default display");

    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_nav_init(&nav, lv_screen_active(), lv_layer_top(), 0),
          "nav_init");

    shrewd_lv_view_shell_t shell;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_shell_create(&shell, disp, &nav, 32, 24),
          "shell_create");
    check(NULL != shell.header, "header exists");
    check(NULL != shell.title, "title exists");
    check(NULL != shell.content, "content exists");
    check(NULL != shell.footer, "footer exists");
    check(shell.content == nav.host, "nav host is the content area");

    /* 挂 home:标题应自动变 */
    static test_page_t home;
    static test_page_t settings;
    page_init(&home, &g_desc_home);
    page_init(&settings, &g_desc_settings);
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &home.base, NULL), "push home");
    check(0 == strcmp("home", lv_label_get_text((const lv_obj_t *)shell.title)),
          "title follows navigation (home)");

    /* 跳 settings:标题自动跟,没手动 set_text */
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_push(&nav, &settings.base, NULL),
          "push settings");
    check(0 == strcmp("settings", lv_label_get_text((const lv_obj_t *)shell.title)),
          "title follows navigation (settings)");

    /* 回退:标题也回 */
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_pop(&nav), "pop");
    check(0 == strcmp("home", lv_label_get_text((const lv_obj_t *)shell.title)),
          "title follows navigation (back to home)");
}

int main(void) {
    lvgl_fixture_init();
    test_shell();
    if (0 == g_fail) {
        printf("test_shell: ok\n");
        return 0;
    }
    printf("test_shell: FAILED (%d)\n", g_fail);
    return 1;
}
