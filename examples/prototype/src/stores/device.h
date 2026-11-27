/**
 * @file device.h
 * @brief 数据模型(stores/):设备状态 + reflect 描述。
 *
 * 按 design.md §9.4 的约定:数据结构含 reflect 描述(可序列化,支撑栈恢复
 * 与参数),应用级共享 ref。**复杂数据走这里,不走导航参数**。
 */
#ifndef PROTOTYPE_DEVICE_H
#define PROTOTYPE_DEVICE_H

#include "shrewd-lv-view/view.h"

/** 一个设备。状态是 ref + 各自的槽位;widget 只是投影。 */
typedef struct {
    int32_t id;
    shrewd_basic_reflect_ref_t name;
    char name_buf[32];
    shrewd_basic_reflect_ref_t battery;
    int32_t battery_val;
} device_t;

/** id 的 reflect 描述(做导航参数、快照用)。 */
extern const shrewd_basic_reflect_type_t prototype_i32_type;

/** 初始化设备状态(ref 指向同 struct 里的槽位)。 */
void device_init(device_t *d, int32_t id, const char *name, int32_t battery);

#endif /* PROTOTYPE_DEVICE_H */
