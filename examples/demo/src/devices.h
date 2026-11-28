/**
 * @file devices.h
 * @brief 演示用的设备数据:ref 就是状态(design.md §5)。
 *
 * 数据在**数据侧线程**里被改(模拟采集),经桥送进 UI 线程 —— 这就是
 * architecture.md §6.8 的两主体形状。
 */
#ifndef DEMO_DEVICES_H
#define DEMO_DEVICES_H

#include <stdint.h>

#include "shrewd-lv-view/view.h"

typedef struct {
    int32_t id;
    shrewd_basic_reflect_ref_t name;
    char name_buf[24];
    shrewd_basic_reflect_ref_t battery;
    int32_t battery_val;
} demo_device_t;

#define DEMO_DEVICE_COUNT 8

extern demo_device_t demo_devices[DEMO_DEVICE_COUNT];

void demo_devices_init(void);

/** 起一条采集线程,把电量经 @p bridge 送进 UI 线程。 */
int demo_feed_start(shrewd_lv_view_bridge_t *bridge);

void demo_feed_stop(void);

#endif /* DEMO_DEVICES_H */
