/**
 * @file theme.h
 * @brief 主题接线与设计 token(design.md §7.6)。
 *
 * **不自造主题引擎** —— 用 LVGL 自己的 `lv_theme_t`:
 *
 *   - **控件**走 LVGL 主题:类上设 `theme_inheritable`,主题的 apply 里
 *     `lv_obj_add_style`,和原生 widget 完全一样;
 *   - **片段**不进主题,而是用**可覆盖的 style 常量表**(设计 token),
 *     创建时套用 —— 因为片段不是 widget 类型,没有 `lv_obj_class_t` 可挂。
 *
 * 两者的样式来源是**同一份 token**,所以整个组件库的间距/圆角/粗细是一套。
 *
 * 装主题要能看到 `lv_theme_t` 的结构定义,那是 **lvgl 的私有头**(§7.7 第三档)。
 */
#ifndef DEMO_THEME_H
#define DEMO_THEME_H

#include "lvgl/lvgl.h"

/** 设计 token:组件库里所有间距/圆角/配色的唯一来源。 */
typedef struct {
    int32_t pad_small;    /**< 紧凑内边距 */
    int32_t pad_def;      /**< 常规内边距 */
    int32_t radius;       /**< 圆角 */
    int32_t border_width; /**< 描边 */
    int32_t card_height;  /**< 卡片(片段)高度 */
    int32_t gauge_height; /**< 水位计(控件)高度 */
} demo_tokens_t;

/** 默认 token。 */
extern const demo_tokens_t demo_tokens_default;

/** **可覆盖**:create 前改它,组件创建时套用(§7.6)。 */
extern demo_tokens_t demo_tokens;

/**
 * 给一块屏幕装上本组件库的主题。父主题是那块屏**当前**的主题(通常是 LVGL
 * 的默认主题)—— 我们只在它之上加自己控件的样子,不覆盖别人。
 */
int demo_theme_init(lv_display_t *disp, const demo_tokens_t *tokens);

#endif /* DEMO_THEME_H */
