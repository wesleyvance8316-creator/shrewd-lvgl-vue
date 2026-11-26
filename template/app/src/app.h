/**
 * @file app.h
 * @brief 装配:初始化 + 挂起始页。两种入口共用。
 */
#ifndef @APP_GUARD @_APP_H
#define @APP_GUARD @_APP_H

#include "shrewd-lv-view/view.h"

/** 初始化 lvgl、建显示、装配应用并挂上首页。 */
int app_boot(shrewd_lv_view_app_t *app);

/** UI 线程的一步:驱动框架的单步(出队 + lv_timer_handler)。 */
int app_step(shrewd_lv_view_app_t *app);

#endif /* @APP_GUARD@_APP_H */
