/**
 * @file home.h
 * @brief 首页 = 片段 + 导航标识(design.md §8.1)。
 */
#ifndef @APP_GUARD @_VIEWS_HOME_H
#define @APP_GUARD @_VIEWS_HOME_H

#include "shrewd-lv-view/view.h"

/**
 * 页面实例:**调用方持有、永久**(design.md §8.3)。释放策略只管 widget 树,
 * 不管实例 —— 所以状态不丢。
 */
typedef struct {
    shrewd_lv_view_page_t base; /* 必须在首位 */
    /* 状态:ref 本体 + 各自的槽位 */
    shrewd_basic_reflect_ref_t title;
    char title_buf[32];
} @APP_PREFIX @_home_t;

/** 初始化实例并挂上描述符(不建 widget —— 那是 create() 的活)。 */
void @APP_PREFIX @_home_init(@APP_PREFIX @_home_t *p);

/** 页面描述符:导航栈靠 name 找回它。 */
extern const shrewd_lv_view_page_desc_t @APP_PREFIX @_home_desc;

#endif /* @APP_GUARD@_VIEWS_HOME_H */
