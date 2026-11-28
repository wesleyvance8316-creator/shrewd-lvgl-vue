/**
 * @file views.h
 * @brief 三个页面:设备列表 / 设备详情 / 设置。
 *
 * 页面 = **状态 struct + 四阶段钩子**(design.md §8.2),实例调用方持有。
 * 这里特意把三件事都摆出来:虚拟化列表、v-model、以及**守卫会看的脏标记**。
 */
#ifndef DEMO_VIEWS_H
#define DEMO_VIEWS_H

#include "devices.h"
#include "shrewd-lv-view/view.h"

/** 设备列表(分支 0):虚拟化列表,点一行进详情。 */
typedef struct {
    shrewd_lv_view_page_t base;
    shrewd_lv_view_nav_t *nav;
    shrewd_lv_view_page_t *detail; /**< 点一行要推进去的页面 */
    lv_obj_t *list;
    shrewd_lv_view_list_t source;
    shrewd_basic_reflect_ref_t source_ref;
} demo_home_page_t;

/** 设备详情:第三方控件 demo_gauge + slider 的 v-model + 脏标记。 */
typedef struct {
    shrewd_lv_view_page_t base;
    shrewd_lv_view_nav_t *nav;
    demo_device_t *dev;
    lv_obj_t *hint; /**< 守卫吃掉请求时在这里提示(§8.5 CONSUME) */
    shrewd_basic_reflect_ref_t level;
    int32_t level_val;
    int32_t level_at_open;
    int dirty;
} demo_detail_page_t;

/** 设置(分支 1)。 */
typedef struct {
    shrewd_lv_view_page_t base;
    shrewd_lv_view_nav_t *nav;
    shrewd_basic_reflect_ref_t volume;
    int32_t volume_val;
} demo_settings_page_t;

void demo_home_init(demo_home_page_t *p, shrewd_lv_view_nav_t *nav, shrewd_lv_view_page_t *detail);
void demo_detail_init(demo_detail_page_t *p, shrewd_lv_view_nav_t *nav);
void demo_settings_init(demo_settings_page_t *p, shrewd_lv_view_nav_t *nav);

/** 守卫要看的:当前详情页是不是没保存(design.md §8.5)。 */
int demo_detail_is_dirty(const demo_detail_page_t *p);

#endif /* DEMO_VIEWS_H */
