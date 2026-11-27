/**
 * @file device_card.c
 */
#include "device_card.h"

#include "shrewd-basic/platform/memory.h"

int device_card_create(device_card_t *c, lv_obj_t *parent, device_t *device) {
    if ((NULL == c) || (NULL == parent) || (NULL == device)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    c->device = device;
    c->root = lv_obj_create(parent);
    lv_obj_set_size(c->root, 180, 48);

    lv_obj_t *label = lv_label_create(c->root);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 6, 4);

    /* 电量用 slider 绑,双向(v-model):改它 = 改设备状态。 */
    lv_obj_t *slider = lv_slider_create(c->root);
    lv_obj_set_size(slider, 160, 8);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 6, 30);
    lv_slider_set_range(slider, 0, 100);

    int status = shrewd_lv_view_bind_label_text(label, &device->name);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    return shrewd_lv_view_bind_slider_value(slider, &device->battery);
}

void device_card_dispose(device_card_t *c) {
    if ((NULL == c) || (NULL == c->root)) {
        return;
    }
    lv_obj_delete(c->root);
    c->root = NULL;
}
