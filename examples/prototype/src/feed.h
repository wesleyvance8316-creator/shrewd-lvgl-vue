/**
 * @file feed.h
 * @brief 模拟数据源:一条业务线程,定时改设备状态。
 *
 * 它在**别的线程**,所以不能直接 ref_set(effect 里是 lv_* —— 铁律)。
 * 走桥:post 入队,UI 线程 drain。
 */
#ifndef PROTOTYPE_FEED_H
#define PROTOTYPE_FEED_H

#include "stores/device.h"

int feed_start(shrewd_lv_view_bridge_t *bridge, device_t *device);
void feed_stop(void);

#endif /* PROTOTYPE_FEED_H */
