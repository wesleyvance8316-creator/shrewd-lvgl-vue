/**
 * @file demo_card.c
 * @brief 组件模型样板的实现。
 */
#include "demo/card.h"

#include <stdio.h>
#include <string.h>

/** 设计 token 的默认值(§7.6)。调用方可在 create 前覆盖。 */
demo_card_style_t demo_card_style = {
    .height = 60,
    .pad = 8,
};

int demo_card_create(demo_card_t *c,
                     lv_obj_t *parent,
                     const demo_card_params_t *params,
                     const demo_card_slots_t *slots) {
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

    /* 2. widget 树 —— 尺寸来自设计 token(§7.6),不写死。 */
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_HOR_RES, demo_card_style.height);
    c->root = root;

    lv_obj_t *label = lv_label_create(root);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, demo_card_style.pad, demo_card_style.pad);

    lv_obj_t *slider = lv_slider_create(root);
    lv_obj_set_size(slider, LV_HOR_RES - (2 * demo_card_style.pad), 12);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, demo_card_style.pad, demo_card_style.height / 2);
    lv_slider_set_range(slider, 0, 100);

    /* 3. 绑定:状态 → widget,双向。绑定随 widget 自清(§3)。 */
    int status = shrewd_lv_view_bind_label_text(label, &c->title);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    /* 组件级 v-model(§7.4):给了外部 ref 就绑它,父不用写"更新"、也不用听"事件"。 */
    shrewd_basic_reflect_ref_t *level
        = (NULL != params && NULL != params->level_ref) ? params->level_ref : &c->level;
    status = shrewd_lv_view_bind_slider_value(slider, level);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }

    /* 4. 槽(§7.5):**一次创建**,子决定何时何处调用。槽内容挂在组件建的
     *    widget 下,所以它的绑定同样随 widget 自清(§3)。 */
    if ((NULL != slots) && (NULL != slots->footer)) {
        lv_obj_t *slot = lv_obj_create(root);
        lv_obj_set_size(slot, LV_HOR_RES, 24);
        lv_obj_align(slot, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        slots->footer(slot, slots->footer_ctx);
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
