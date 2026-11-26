/**
 * @file lvgl_fixture.h
 * @brief 无头 LVGL 夹具(design.md §13)。
 *
 * 单测不需要显示器:一块小 draw buffer + 空 flush + 假 tick 就够。
 * 断言一律查状态(LVGL 属性读回 / ref 槽位),不模拟渲染结果。
 */
#ifndef SHREWD_LV_VIEW_TESTS_LVGL_FIXTURE_H
#define SHREWD_LV_VIEW_TESTS_LVGL_FIXTURE_H

#include "lvgl/lvgl.h"

/** lv_init + 建假显示 + 假 tick。每个测试进程调一次。 */
void lvgl_fixture_init(void);

/** 走一轮 lv_timer_handler(驱动定时器、事件、刷新)。 */
void lvgl_fixture_step(void);

/** 推进假时钟 @p ms 毫秒。 */
void lvgl_fixture_advance(unsigned ms);

#endif /* SHREWD_LV_VIEW_TESTS_LVGL_FIXTURE_H */
