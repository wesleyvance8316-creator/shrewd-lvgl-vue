/**
 * @file lv_view_shell.c
 * @brief 壳:标题栏 + 底栏 + 内容区(architecture.md §6.9)。
 *
 * 壳是**持久层** —— 不进页面栈、一直活着。页面渲染在中间的内容区
 * (`nav->host` 指到它)。壳的 UI 是**导航状态的绑定**:标题栏显当前页名,
 * 靠 `shrewd_lv_view_bind_label_text` 绑 `nav->current_name`,导航变了自动跟。
 */

#include <string.h>

#include "shrewd-lv-view/view.h"

int shrewd_lv_view_shell_create(shrewd_lv_view_shell_t *shell,
                                lv_display_t *display,
                                shrewd_lv_view_nav_t *nav,
                                int32_t header_height,
                                int32_t footer_height) {
    if ((NULL == shell) || (NULL == display) || (NULL == nav)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    memset(shell, 0, sizeof(*shell));
    lv_obj_t *screen = lv_display_get_screen_active(display);
    if (NULL == screen) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    int32_t w = lv_display_get_horizontal_resolution(display);
    int32_t h = lv_display_get_vertical_resolution(display);

    shell->root = lv_obj_create(screen);
    lv_obj_set_size(shell->root, w, h);
    lv_obj_set_style_pad_all(shell->root, 0, 0);
    lv_obj_set_style_border_width(shell->root, 0, 0);
    lv_obj_set_flex_flow(shell->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        shell->root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    /* 标题栏:持久。 */
    shell->header = lv_obj_create(shell->root);
    lv_obj_set_size(shell->header, w, (0 < header_height) ? header_height : 32);
    lv_obj_set_style_pad_all(shell->header, 0, 0);
    lv_obj_set_style_border_width(shell->header, 0, 0);

    shell->title = lv_label_create(shell->header);
    lv_obj_align(shell->title, LV_ALIGN_LEFT_MID, 8, 0);

    /* 内容区:页面就渲染在这里,它就是 nav->host。 */
    shell->content = lv_obj_create(shell->root);
    lv_obj_set_size(shell->content,
                    w,
                    h - ((0 < header_height) ? header_height : 32)
                        - ((0 < footer_height) ? footer_height : 0));
    lv_obj_set_style_pad_all(shell->content, 0, 0);
    lv_obj_set_style_border_width(shell->content, 0, 0);

    /* 底栏:持久。0 高度表示不要。子项横排,正好是 tab 栏。 */
    if (0 < footer_height) {
        shell->footer = lv_obj_create(shell->root);
        lv_obj_set_size(shell->footer, w, footer_height);
        lv_obj_set_style_pad_all(shell->footer, 0, 0);
        lv_obj_set_style_border_width(shell->footer, 0, 0);
        lv_obj_set_flex_flow(shell->footer, LV_FLEX_FLOW_ROW);
    }

    /* 壳的 UI 是导航状态的绑定 —— 标题栏不用手动同步。 */
    int status = shrewd_lv_view_bind_label_text(shell->title, &nav->current_name);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    /* 导航的页面容器就是内容区。 */
    nav->host = shell->content;
    /* add_tab 要靠它切分支。 */
    shell->nav = nav;
    return SHREWD_LV_VIEW_ERR_OK;
}

/** tab 按钮被点:切到它对应的分支。index 从"是第几个子项"来。 */
static void shell_tab_clicked(lv_event_t *e) {
    shrewd_lv_view_shell_t *shell = (shrewd_lv_view_shell_t *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_current_target_obj(e);
    int32_t index = lv_obj_get_index(btn);
    if ((NULL == shell) || (NULL == shell->nav) || (0 > index)) {
        return;
    }
    (void)shrewd_lv_view_nav_switch_branch(shell->nav, (size_t)index);
}

int shrewd_lv_view_shell_add_tab(shrewd_lv_view_shell_t *shell, const char *label) {
    if ((NULL == shell) || (NULL == shell->footer) || (NULL == shell->nav) || (NULL == label)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (SHREWD_LV_VIEW_NAV_BRANCHES <= shell->tab_count) {
        return SHREWD_LV_VIEW_ERR_FULL;
    }
    lv_obj_t *btn = lv_button_create(shell->footer);
    if (NULL == btn) {
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM; /* LVGL 没有错误码,只能自己起名(§12) */
    }
    lv_obj_t *text = lv_label_create(btn);
    if (NULL == text) {
        lv_obj_delete(btn);
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    lv_label_set_text(text, label);
    lv_obj_center(text);
    lv_obj_add_event_cb(btn, shell_tab_clicked, LV_EVENT_CLICKED, shell);

    /* 高亮是个绑定,不是手动同步:只绑一次,底栏的子项按分支顺序 = tab。 */
    if (0 == shell->tab_count) {
        int status = shrewd_lv_view_bind_tab_bar(shell->footer, &shell->nav->current_branch);
        if (SHREWD_LV_VIEW_ERR_OK != status) {
            return status;
        }
    }
    shell->tab_count += 1;
    return SHREWD_LV_VIEW_ERR_OK;
}
