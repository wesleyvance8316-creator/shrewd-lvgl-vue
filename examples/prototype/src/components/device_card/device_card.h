/**
 * @file device_card.h
 * @brief 组件(design.md §7.2):状态 struct + 创建/销毁。
 */
#ifndef PROTOTYPE_DEVICE_CARD_H
#define PROTOTYPE_DEVICE_CARD_H

#include "stores/device.h"

/** 组件实例:调用方内嵌。 */
typedef struct {
    lv_obj_t *root;
    device_t *device; /* 指向数据源里的设备;状态不在这里 */
} device_card_t;

int device_card_create(device_card_t *c, lv_obj_t *parent, device_t *device);
void device_card_dispose(device_card_t *c);

#endif /* PROTOTYPE_DEVICE_CARD_H */
