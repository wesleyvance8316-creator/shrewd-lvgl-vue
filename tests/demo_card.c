/**
 * @file demo_card.c
 * @brief 组件模型样板的实现。
 */
#include "demo_card.h"

#include <stdio.h>
#include <string.h>

int demo_card_create(demo_card_t *c, lv_obj_t *parent, const demo_card_params_t *params) {
    if ((NULL == c) || (NULL == parent)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    memset(c, 0, sizeof(*c));

    /* 1. 状态:ref 指向同 struct 里的槽位(§design.md 5)。 */
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_reflect_ref_init(&c->title, c->title_buf, sizeof(c->title_buf))) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_reflect_ref_init(&c->level, &c->level_val, sizeof(c->level_val))) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (NULL != params) {
        (void)strncpy(c->title_buf, params->title, sizeof(c->title_buf) - 1);
        c->level_val = params->level;
    }

    /* 2. widget 树。 */
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_HOR_RES, 60);
    c->root = root;

    lv_obj_t *label = lv_label_create(root);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 8, 8);

    lv_obj_t *slider = lv_slider_create(root);
    lv_obj_set_size(slider, LV_HOR_RES - 32, 12);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 8, 32);
    lv_slider_set_range(slider, 0, 100);

    /* 3. 绑定:状态 → widget,双向。绑定随 widget 自清(§3)。 */
    int status = shrewd_lv_view_bind_label_text(label, &c->title);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    status = shrewd_lv_view_bind_slider_value(slider, &c->level);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    return SHREWD_LV_VIEW_ERR_OK;
}

void demo_card_dispose(demo_card_t *c) {
    if ((NULL == c) || (NULL == c->root)) {
        return;
    }
    lv_obj_delete(c->root);
    c->root = NULL;
    /* 绑定已随 widget 级联自清(§3),这里不要手动 dispose。 */
}
