/**
 * @file demo_card.h
 * @brief 组件模型的样板(design.md §7.2)。
 *
 * 组件 = 状态 struct + 创建函数 + 销毁函数,状态 struct **调用方内嵌**。
 * 这里用一张卡片演示三件事:
 *
 *   - 状态是 ref + 各自的槽位(§design.md 5),widget 只是投影;
 *   - 创建函数建 widget 树并逐个绑定,**绑定随 widget 自清**(§3),
 *     销毁函数因此只需删 root;
 *   - 参数是可序列化的简单值(§design.md 8.3 / 6.11 Q2)。
 *
 * 页面是"片段 + 导航标识"(§8.1);片段本身不带导航,只是被别人用。
 */
#ifndef SHREWD_LV_VIEW_TESTS_DEMO_CARD_H
#define SHREWD_LV_VIEW_TESTS_DEMO_CARD_H

#include "shrewd-lv-view/view.h"

/** 创建参数:可序列化的简单值。 */
typedef struct {
    const char *title;
    int32_t level;
} demo_card_params_t;

/** 组件实例:调用方内嵌。 */
typedef struct {
    lv_obj_t *root;
    /* 状态:ref 本体 + 各自的槽位 */
    shrewd_basic_reflect_ref_t title;
    char title_buf[32];
    shrewd_basic_reflect_ref_t level;
    int32_t level_val;
} demo_card_t;

/** 建 widget 树 + 挂绑定。@p params 为 NULL 时用零值。 */
int demo_card_create(demo_card_t *c, lv_obj_t *parent, const demo_card_params_t *params);

/** 删 root;绑定级联自清,不要手动 dispose 绑定(§3)。 */
void demo_card_dispose(demo_card_t *c);

#endif /* SHREWD_LV_VIEW_TESTS_DEMO_CARD_H */
