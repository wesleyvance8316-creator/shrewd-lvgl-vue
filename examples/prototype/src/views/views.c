/**
 * @file views.c
 */
#include "views/views.h"

#include <stdio.h>

#include "shrewd-basic/platform/memory.h"
#include <string.h>

#include "components/device_card/device_card.h"

/* ============================ home:设备列表 ============================ */

typedef struct {
    device_t **devices;
    uint32_t count;
} home_source_t;

static const void *home_at(void *ctx, uint32_t i) {
    home_source_t *s = (home_source_t *)ctx;
    return s->devices[i]; /* 返回 device_t * */
}

static uint32_t home_key(void *ctx, uint32_t i) {
    home_source_t *s = (home_source_t *)ctx;
    return (uint32_t)s->devices[i]->id;
}

/*
 * ★ API 摩擦点(design.md §10.3 的反例):
 *   组件实例本该"调用方内嵌"(§7.2),但**列表子项是动态的,调用方无法预先持有**。
 *   这里只好自己分配 card、并把它塞进 widget 的 user_data,teardown 再取回释放。
 *   若框架给子项一个归属槽,这层记账就没了。
 */
static lv_obj_t *home_build(void *ctx, lv_obj_t *container, const void *item) {
    (void)ctx;
    device_t *d = (device_t *)item;
    void *slot = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_calloc(&slot, 1, sizeof(device_card_t))) {
        return NULL;
    }
    device_card_t *c = (device_card_t *)slot;
    if (SHREWD_LV_VIEW_ERR_OK != device_card_create(c, container, d)) {
        (void)shrewd_basic_platform_free(&slot);
        return NULL;
    }
    lv_obj_set_user_data(c->root, c); /* 取回用 */
    return c->root;
}

static void home_teardown(void *ctx, lv_obj_t *root, const void *item) {
    (void)ctx;
    (void)item;
    device_card_t *c = (device_card_t *)lv_obj_get_user_data(root);
    if (NULL != c) {
        void *slot = c;
        (void)shrewd_basic_platform_free(&slot);
    }
}

static const shrewd_lv_view_list_desc_t home_list_desc = {
    .at = home_at,
    .key = home_key,
    .item_size = sizeof(device_t *),
    .build = home_build,
    .teardown = home_teardown,
};

static int home_create(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params) {
    (void)params;
    home_page_t *h = (home_page_t *)p;
    lv_obj_t *root = lv_obj_create(container);
    lv_obj_set_size(root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(root, 4, 0);
    p->root = root;

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Devices");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 4);

    lv_obj_t *list = lv_obj_create(root);
    lv_obj_set_size(list, LV_HOR_RES - 16, LV_VER_RES - 40);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 4, 28);

    /* 列表对账:结构变化(增删/重排)走 source ref。 */
    h->list.items = h->devices;
    h->list.count = h->count;
    (void)shrewd_basic_reflect_ref_init(&h->source, &h->list, sizeof(h->list));
    return shrewd_lv_view_bind_list(list, &h->source, &home_list_desc, h);
}

static int home_enter(shrewd_lv_view_page_t *p) {
    home_page_t *h = (home_page_t *)p;
    /* 激活时把当前数据源推给列表(结构变化)。 */
    h->list.items = h->devices;
    h->list.count = h->count;
    return shrewd_basic_reflect_ref_set(&h->source, &h->list, sizeof(h->list));
}

static void home_exit(shrewd_lv_view_page_t *p) {
    (void)p;
}

static void home_destroy(shrewd_lv_view_page_t *p) {
    (void)p;
}

const shrewd_lv_view_page_desc_t prototype_home_desc = {
    .name = "home",
    .param_type = NULL,
    .release = SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT,
    .cache_count = 0,
    .create = home_create,
    .enter = home_enter,
    .exit = home_exit,
    .destroy = home_destroy,
};

void home_page_init(home_page_t *p, device_t **devices, uint32_t count) {
    memset(p, 0, sizeof(*p));
    p->base.desc = &prototype_home_desc;
    p->devices = devices;
    p->count = count;
}

/* ============================ detail:v-model ============================ */

static int detail_create(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params) {
    detail_page_t *d = (detail_page_t *)p;
    if (NULL != params) {
        d->device_id = *(const int32_t *)params;
    }
    lv_obj_t *root = lv_obj_create(container);
    lv_obj_set_size(root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(root, 8, 0);
    p->root = root;

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Device detail");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* v-model:改 slider = 改设备状态,反向也成立。 */
    lv_obj_t *slider = lv_slider_create(root);
    lv_obj_set_size(slider, LV_HOR_RES - 32, 12);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 8, 32);
    lv_slider_set_range(slider, 0, 100);
    return shrewd_lv_view_bind_slider_value(slider, &d->device->battery);
}

static int detail_enter(shrewd_lv_view_page_t *p) {
    (void)p;
    return SHREWD_LV_VIEW_ERR_OK;
}

static void detail_exit(shrewd_lv_view_page_t *p) {
    (void)p;
}

static void detail_destroy(shrewd_lv_view_page_t *p) {
    (void)p;
}

const shrewd_lv_view_page_desc_t prototype_detail_desc = {
    .name = "detail",
    .param_type = &prototype_i32_type,
    .release = SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT,
    .cache_count = 0,
    .create = detail_create,
    .enter = detail_enter,
    .exit = detail_exit,
    .destroy = detail_destroy,
};

void detail_page_init(detail_page_t *p, device_t **devices, uint32_t count) {
    memset(p, 0, sizeof(*p));
    p->base.desc = &prototype_detail_desc;
    /* 简化:取第一个设备;真实里按 id 查。 */
    if (0 < count) {
        p->device = devices[0];
    }
}

/* ============================ settings:文本 ============================ */

static int settings_create(shrewd_lv_view_page_t *p, lv_obj_t *container, const void *params) {
    (void)params;
    settings_page_t *s = (settings_page_t *)p;
    lv_obj_t *root = lv_obj_create(container);
    lv_obj_set_size(root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(root, 8, 0);
    p->root = root;

    lv_obj_t *label = lv_label_create(root);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *area = lv_textarea_create(root);
    lv_obj_set_size(area, LV_HOR_RES - 32, 40);
    lv_obj_align(area, LV_ALIGN_TOP_LEFT, 8, 32);

    int status = shrewd_lv_view_bind_label_text(label, &s->title);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    /* v-model:textarea 文本 ↔ ref。 */
    return shrewd_lv_view_bind_textarea_text(area, &s->title);
}

static int settings_enter(shrewd_lv_view_page_t *p) {
    (void)p;
    return SHREWD_LV_VIEW_ERR_OK;
}

static void settings_exit(shrewd_lv_view_page_t *p) {
    (void)p;
}

static void settings_destroy(shrewd_lv_view_page_t *p) {
    (void)p;
}

const shrewd_lv_view_page_desc_t prototype_settings_desc = {
    .name = "settings",
    .param_type = NULL,
    .release = SHREWD_LV_VIEW_RELEASE_RESIDENT,
    .cache_count = 0,
    .create = settings_create,
    .enter = settings_enter,
    .exit = settings_exit,
    .destroy = settings_destroy,
};

void settings_page_init(settings_page_t *p) {
    memset(p, 0, sizeof(*p));
    p->base.desc = &prototype_settings_desc;
    (void)shrewd_basic_reflect_ref_init(&p->title, p->title_buf, sizeof(p->title_buf));
    (void)snprintf(p->title_buf, sizeof(p->title_buf), "settings");
}
