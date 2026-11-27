/**
 * @file test_shell.c
 * @brief 壳的单测(architecture.md §6.9)。
 *
 * 验四件事:壳的三段结构、nav->host 指到内容区、**标题跟着导航自动变**、
 * **底栏 tab 高亮跟着 current_branch 自动变**(都是绑定,不是手动同步)。
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

/* --- tab 高亮:机制(bind_tab_bar)------------------------------------ */

static int is_checked(lv_obj_t *bar, uint32_t i) {
    lv_obj_t *child = lv_obj_get_child(bar, (int32_t)i);
    return (NULL != child) && lv_obj_has_state(child, LV_STATE_CHECKED);
}

static void test_tab_bar_target(void) {
    printf("test_tab_bar_target\n");
    lv_obj_t *bar = lv_obj_create(lv_screen_active());
    for (int i = 0; i < 3; ++i) {
        (void)lv_button_create(bar);
    }
    uint32_t branch = 0;
    shrewd_basic_reflect_ref_t ref;
    check(SHREWD_BASIC_ERR_OK == shrewd_basic_reflect_ref_init(&ref, &branch, sizeof(branch)),
          "ref_init");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_bind_tab_bar(bar, &ref), "bind_tab_bar");

    /* 绑定建立时就该跑一次:0 亮 */
    check(is_checked(bar, 0) && !is_checked(bar, 1) && !is_checked(bar, 2), "branch 0 lit");

    branch = 2;
    (void)shrewd_basic_reflect_ref_set(&ref, &branch, sizeof(branch));
    check(!is_checked(bar, 0) && !is_checked(bar, 1) && is_checked(bar, 2), "branch 2 lit");

    branch = 1;
    (void)shrewd_basic_reflect_ref_set(&ref, &branch, sizeof(branch));
    check(!is_checked(bar, 0) && is_checked(bar, 1) && !is_checked(bar, 2), "branch 1 lit");
}

/* --- tab 高亮:壳的便捷(shell_add_tab)------------------------------- */

static void test_shell_tabs(void) {
    printf("test_shell_tabs\n");
    lv_display_t *disp = lv_display_get_default();
    shrewd_lv_view_nav_t nav;
    check(SHREWD_LV_VIEW_ERR_OK
              == shrewd_lv_view_nav_init(&nav, lv_screen_active(), lv_layer_top(), 0),
          "nav_init");
    shrewd_lv_view_shell_t shell;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_shell_create(&shell, disp, &nav, 32, 24),
          "shell_create");
    check(&nav == shell.nav, "shell remembers nav");

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_shell_add_tab(&shell, "home"), "add tab 0");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_shell_add_tab(&shell, "settings"), "add tab 1");
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_shell_add_tab(&shell, "about"), "add tab 2");
    check(3 == lv_obj_get_child_count(shell.footer), "3 tab buttons");
    check(3 == shell.tab_count, "tab_count 3");

    /* 初始分支 0 → tab 0 亮。全程没手动改样式。 */
    check(is_checked(shell.footer, 0) && !is_checked(shell.footer, 1)
              && !is_checked(shell.footer, 2),
          "tab 0 lit on branch 0");

    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_nav_switch_branch(&nav, 2), "switch to 2");
    check(!is_checked(shell.footer, 0) && is_checked(shell.footer, 2), "tab 2 lit on branch 2");

    /* 点 tab 0:切分支 + 高亮跟着走 —— 一次点击两个都验 */
    lv_obj_t *tab0 = lv_obj_get_child(shell.footer, 0);
    lv_obj_send_event(tab0, LV_EVENT_CLICKED, NULL);
    check(0 == nav.current, "click switched to branch 0");
    check(is_checked(shell.footer, 0) && !is_checked(shell.footer, 2),
          "highlight followed the click");

    /* 没底栏时加 tab 该报错,不是崩 */
    shrewd_lv_view_shell_t bare;
    check(SHREWD_LV_VIEW_ERR_OK == shrewd_lv_view_shell_create(&bare, disp, &nav, 32, 0),
          "shell without footer");
    check(SHREWD_LV_VIEW_ERR_ARG_INVALID == shrewd_lv_view_shell_add_tab(&bare, "x"),
          "add_tab without footer is rejected");
}

int main(void) {
    lvgl_fixture_init();
    test_shell();
    test_tab_bar_target();
    test_shell_tabs();
    if (0 == g_fail) {
        printf("test_shell: ok\n");
        return 0;
    }
    printf("test_shell: FAILED (%d)\n", g_fail);
    return 1;
}
