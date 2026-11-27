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

    /* 底栏:持久。0 高度表示不要。 */
    if (0 < footer_height) {
        shell->footer = lv_obj_create(shell->root);
        lv_obj_set_size(shell->footer, w, footer_height);
        lv_obj_set_style_pad_all(shell->footer, 0, 0);
        lv_obj_set_style_border_width(shell->footer, 0, 0);
    }

    /* 壳的 UI 是导航状态的绑定 —— 标题栏不用手动同步。 */
    int status = shrewd_lv_view_bind_label_text(shell->title, &nav->current_name);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    /* 导航的页面容器就是内容区。 */
    nav->host = shell->content;
    return SHREWD_LV_VIEW_ERR_OK;
}
