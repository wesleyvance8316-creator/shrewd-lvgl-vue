/**
 * @file home.c
 * @brief 首页的四阶段钩子(design.md §8.2)。
 */
#include "home.h"

#include <stdio.h>
#include <string.h>

static int home_create(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params) {
    (void)params;
    @APP_PREFIX @_home_t *self = (@APP_PREFIX @_home_t *)p;

    lv_obj_t *root = lv_obj_create(container);
    lv_obj_set_size(root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    p->root = root;

    lv_obj_t *label = lv_label_create(root);
    lv_obj_center(label);
    /* 绑定:状态 → widget。这里是直接写;接上 shrewd_lv_view_bind_* 后自动传播。 */
    lv_label_set_text(label, self->title_buf);
    return SHREWD_LV_VIEW_ERR_OK;
}

static int home_enter(shrewd_lv_view_page_t *p) {
    (void)p;
    /* 订阅 / 加载。 */
    return SHREWD_LV_VIEW_ERR_OK;
}

static void home_exit(shrewd_lv_view_page_t *p) {
    (void)p;
    /* 退订 / 保存。 */
}

static void home_destroy(shrewd_lv_view_page_t *p) {
    (void)p;
    /* 绑定随 widget 自清(design.md §3),这里不用手动解绑。 */
}

const shrewd_lv_view_page_desc_t @APP_PREFIX @_home_desc = {
    .name = "home",
    .param_type = NULL,
    .create = home_create,
    .enter = home_enter,
    .exit = home_exit,
    .destroy = home_destroy,
};

void @APP_PREFIX @_home_init(@APP_PREFIX @_home_t *p) {
    memset(p, 0, sizeof(*p));
    p->base.desc = &@APP_PREFIX @_home_desc;
    /* 状态:ref 指向同 struct 里的槽位。 */
    (void)shrewd_basic_reflect_ref_init(&p->title, p->title_buf, sizeof(p->title_buf));
    (void)snprintf(p->title_buf, sizeof(p->title_buf), "%s", "@APP_NAME@");
}
