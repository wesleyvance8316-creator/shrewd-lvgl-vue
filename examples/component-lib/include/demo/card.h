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
#ifndef DEMO_COMPONENTS_DEMO_CARD_H
#define DEMO_COMPONENTS_DEMO_CARD_H

#include "demo/theme.h"
#include "shrewd-lv-view/view.h"

/** 创建参数:可序列化的简单值(§7.2)。 */
typedef struct {
    const char *title;
    int32_t level;
    /**
     * 组件级 v-model(§7.4):**传 ref 即双向**。给了它,内部就绑它,不再用
     * 自己的 `level_val` —— 父既不写"更新"也不听"事件"。
     * **不参与序列化**(它是活绑定,不是值),所以带 v-model 的组件不能只靠 params 恢复。
     */
    shrewd_basic_reflect_ref_t *level_ref;
} demo_card_params_t;

/** 槽(§7.5):父提供内容的构建方式,子决定何时、何处调用一次。 */
typedef struct {
    shrewd_lv_view_slot_fn footer;
    void *footer_ctx;
} demo_card_slots_t;

/*
 * 设计 token 见 <demo/theme.h>:**片段与控件共用一份**。
 * 要改就在 create 前改 `demo_tokens` —— 组件创建时套用(§7.6)。
 */

/** 组件实例:调用方内嵌。 */
typedef struct {
    lv_obj_t *root;
    /* 状态:ref 本体 + 各自的槽位 */
    shrewd_basic_reflect_ref_t title;
    char title_buf[32];
    shrewd_basic_reflect_ref_t level;
    int32_t level_val;
} demo_card_t;

/**
 * 建 widget 树 + 挂绑定。@p params / @p slots 为 NULL 时用默认。
 * 槽只在创建时调用**一次**(§7.5);槽内容是组件建的 widget 的子节点,
 * 所以它的绑定随 widget 自清(§3)。
 */
int demo_card_create(demo_card_t *c,
                     lv_obj_t *parent,
                     const demo_card_params_t *params,
                     const demo_card_slots_t *slots);

/** 删 root;绑定级联自清,不要手动 dispose 绑定(§3)。 */
void demo_card_dispose(demo_card_t *c);

#endif /* DEMO_COMPONENTS_DEMO_CARD_H */
