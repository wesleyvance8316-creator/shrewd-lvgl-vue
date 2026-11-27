/**
 * @file views.h
 * @brief 三个页面:home(列表)/ detail(v-model)/ settings(文本)。
 */
#ifndef PROTOTYPE_VIEWS_H
#define PROTOTYPE_VIEWS_H

#include "stores/device.h"

/* --- home:设备列表(列表对账)---------------------------------------- */
typedef struct {
    shrewd_lv_view_page_t base;
    shrewd_lv_view_list_t list; /* ref 槽位 */
    shrewd_basic_reflect_ref_t source;
    device_t **devices;
    uint32_t count;
} home_page_t;

void home_page_init(home_page_t *p, device_t **devices, uint32_t count);

/* --- detail:设备详情(v-model)-------------------------------------- */
typedef struct {
    shrewd_lv_view_page_t base;
    device_t *device;
    int32_t device_id;
} detail_page_t;

void detail_page_init(detail_page_t *p, device_t **devices, uint32_t count);

/* --- settings:设置(文本绑定)--------------------------------------- */
typedef struct {
    shrewd_lv_view_page_t base;
    shrewd_basic_reflect_ref_t title;
    char title_buf[32];
} settings_page_t;

void settings_page_init(settings_page_t *p);

#endif /* PROTOTYPE_VIEWS_H */
