/**
 * @file views.c
 * @brief 三个页面的实现。
 *
 * 这里同时演示四件事:
 *   - **虚拟化列表**(`item_height`):设备再多也只建可视那几行;
 *   - **v-model**:slider 直接绑 ref,父不写"更新"也不听"事件";
 *   - **第三方控件**:`demo_gauge` 是组件库出的 widget,和原生 widget 同场;
 *   - **脏标记**:守卫(§8.5)靠它决定让不让走。
 */
#include "views.h"

#include <stdio.h>
#include <string.h>

#include "demo/gauge.h"

/* ============================ 设备列表 ============================ */

typedef struct {
    lv_obj_t *root;
    lv_obj_t *name_label;
    lv_obj_t *battery_label;
    demo_device_t *dev;
    demo_home_page_t *page;
} row_t;

static const void *row_at(void *ctx, uint32_t i) {
    (void)ctx;
    return &demo_devices[i];
}

static uint32_t row_key(void *ctx, uint32_t i) {
    (void)ctx;
    return (uint32_t)demo_devices[i].id;
}

static void row_clicked(lv_event_t *e) {
    row_t *row = (row_t *)lv_event_get_user_data(e);
    if ((NULL == row) || (NULL == row->page) || (NULL == row->dev)) {
        return;
    }
    /* 点一行 = 前进(不是返回),所以是 push。 */
    (void)shrewd_lv_view_nav_push(row->page->nav, row->page->detail, &row->dev->id);
}

static lv_obj_t *row_build(void *ctx, lv_obj_t *container, const void *item, void *instance) {
    (void)ctx;
    row_t *row = (row_t *)instance;
    if (NULL == row) {
        return NULL;
    }
    row->dev = (demo_device_t *)item;
    row->page = (demo_home_page_t *)ctx; /* ctx 就是所属的页面 */
    row->root = lv_obj_create(container);
    lv_obj_set_style_pad_all(row->root, 4, 0);

    row->name_label = lv_label_create(row->root);
    lv_obj_align(row->name_label, LV_ALIGN_LEFT_MID, 4, 0);
    row->battery_label = lv_label_create(row->root);
    lv_obj_align(row->battery_label, LV_ALIGN_RIGHT_MID, -4, 0);

    /* 内容变化走反应式:绑 item 里的 ref,不用回 build。 */
    (void)shrewd_lv_view_bind_label_text(row->name_label, &row->dev->name);
    (void)shrewd_lv_view_bind_label_text(row->battery_label, &row->dev->battery);

    lv_obj_add_event_cb(row->root, row_clicked, LV_EVENT_CLICKED, row);
    return row->root;
}

static void row_teardown(void *ctx, void *instance) {
    (void)ctx;
    row_t *row = (row_t *)instance;
    if (NULL != row) {
        row->dev = NULL;
    }
}

static const shrewd_lv_view_list_desc_t g_rows = {
    .at = row_at,
    .key = row_key,
    .item_size = sizeof(demo_device_t),
    .instance_size = sizeof(row_t),
    .item_height = 56, /* > 0 = 虚拟化 */
    .build = row_build,
    .teardown = row_teardown,
};

static int home_create(shrewd_lv_view_page_t *base, lv_obj_t *container, const void *params) {
    (void)params;
    demo_home_page_t *p = (demo_home_page_t *)base;
    p->base.root = lv_obj_create(container);
    lv_obj_set_style_pad_all(p->base.root, 0, 0);
    lv_obj_set_style_border_width(p->base.root, 0, 0);

    p->list = lv_obj_create(p->base.root);
    lv_obj_set_size(p->list, 440, 220);
    lv_obj_align(p->list, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_all(p->list, 0, 0);

    (void)shrewd_basic_reflect_ref_init(&p->source_ref, &p->source, sizeof(p->source));
    return shrewd_lv_view_bind_list(p->list, &p->source_ref, &g_rows, p);
}

static int home_enter(shrewd_lv_view_page_t *base) {
    demo_home_page_t *p = (demo_home_page_t *)base;
    /* 结构变化走对账:把数据给上,列表自己增删复用。 */
    p->source.items = demo_devices;
    p->source.count = DEMO_DEVICE_COUNT;
    return shrewd_basic_reflect_ref_set(&p->source_ref, &p->source, sizeof(p->source));
}

static const shrewd_lv_view_page_desc_t g_home_desc = {
    .name = "devices",
    .param_type = NULL,
    .release = SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT,
    .create = home_create,
    .enter = home_enter,
};

/* ============================ 设备详情 ============================ */

static void detail_back(lv_event_t *e) {
    demo_detail_page_t *p = (demo_detail_page_t *)lv_event_get_user_data(e);
    if ((NULL != p) && (NULL != p->nav)) {
        (void)shrewd_lv_view_nav_pop(p->nav); /* 守卫会在这儿被问一次 */
    }
}

static int detail_create(shrewd_lv_view_page_t *base, lv_obj_t *container, const void *params) {
    demo_detail_page_t *p = (demo_detail_page_t *)base;
    if (NULL != params) {
        int32_t id = *(const int32_t *)params;
        p->dev = &demo_devices[id - 1];
    }
    /* v-model:本地 ref 交给 slider 与 gauge,父不写"更新"。 */
    p->level_val = (NULL != p->dev) ? p->dev->battery_val : 0;
    p->level_at_open = p->level_val;
    p->dirty = 0;
    (void)shrewd_basic_reflect_ref_init(&p->level, &p->level_val, sizeof(p->level_val));

    p->base.root = lv_obj_create(container);
    lv_obj_set_style_pad_all(p->base.root, 8, 0);

    lv_obj_t *title = lv_label_create(p->base.root);
    lv_label_set_text(title, (NULL != p->dev) ? p->dev->name_buf : "detail");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 第三方控件:组件库出的 widget,跟原生的一样用。 */
    lv_obj_t *gauge = demo_gauge_create(p->base.root);
    lv_obj_set_size(gauge, 300, 16);
    lv_obj_align(gauge, LV_ALIGN_TOP_LEFT, 0, 32);
    (void)demo_bind_gauge_value(gauge, &p->level);

    lv_obj_t *slider = lv_slider_create(p->base.root);
    lv_obj_set_size(slider, 300, 12);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 0, 60);
    lv_slider_set_range(slider, 0, 100);
    (void)shrewd_lv_view_bind_slider_value(slider, &p->level);

    p->hint = lv_label_create(p->base.root);
    lv_label_set_text(p->hint, "");
    lv_obj_align(p->hint, LV_ALIGN_TOP_LEFT, 120, 96);

    lv_obj_t *back = lv_button_create(p->base.root);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 0, 92);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, "返回");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back, detail_back, LV_EVENT_CLICKED, p);
    return SHREWD_LV_VIEW_ERR_OK;
}

static int detail_enter(shrewd_lv_view_page_t *base) {
    demo_detail_page_t *p = (demo_detail_page_t *)base;
    p->level_at_open = p->level_val;
    p->dirty = 0;
    return SHREWD_LV_VIEW_ERR_OK;
}

static void detail_exit(shrewd_lv_view_page_t *base) {
    demo_detail_page_t *p = (demo_detail_page_t *)base;
    p->dirty = (p->level_val != p->level_at_open);
}

int demo_detail_is_dirty(const demo_detail_page_t *p) {
    /* 实时比,不靠 exit 记的标志 —— 守卫是在**离开之前**被问的。 */
    return (NULL != p) && (p->level_val != p->level_at_open);
}

static const shrewd_lv_view_page_desc_t g_detail_desc = {
    .name = "detail",
    .param_type = NULL,
    .release = SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT,
    .create = detail_create,
    .enter = detail_enter,
    .exit = detail_exit,
};

/* ============================ 设置 ============================ */

static void settings_home(lv_event_t *e) {
    demo_settings_page_t *p = (demo_settings_page_t *)lv_event_get_user_data(e);
    if ((NULL != p) && (NULL != p->nav)) {
        (void)shrewd_lv_view_nav_switch_branch(p->nav, 0);
    }
}

static int settings_create(shrewd_lv_view_page_t *base, lv_obj_t *container, const void *params) {
    (void)params;
    demo_settings_page_t *p = (demo_settings_page_t *)base;
    p->volume_val = 40;
    (void)shrewd_basic_reflect_ref_init(&p->volume, &p->volume_val, sizeof(p->volume_val));

    p->base.root = lv_obj_create(container);
    lv_obj_set_style_pad_all(p->base.root, 8, 0);

    lv_obj_t *label = lv_label_create(p->base.root);
    lv_label_set_text(label, "音量");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *slider = lv_slider_create(p->base.root);
    lv_obj_set_size(slider, 300, 12);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_slider_set_range(slider, 0, 100);
    (void)shrewd_lv_view_bind_slider_value(slider, &p->volume);

    lv_obj_t *btn = lv_button_create(p->base.root);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 56);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "回到设备");
    lv_obj_center(btn_label);
    lv_obj_add_event_cb(btn, settings_home, LV_EVENT_CLICKED, p);
    return SHREWD_LV_VIEW_ERR_OK;
}

static const shrewd_lv_view_page_desc_t g_settings_desc = {
    .name = "settings",
    .param_type = NULL,
    .release = SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT,
    .create = settings_create,
};

/* ============================ 装配 ============================ */

void demo_home_init(demo_home_page_t *p, shrewd_lv_view_nav_t *nav, shrewd_lv_view_page_t *detail) {
    memset(p, 0, sizeof(*p));
    p->base.desc = &g_home_desc;
    p->nav = nav;
    p->detail = detail;
}

void demo_detail_init(demo_detail_page_t *p, shrewd_lv_view_nav_t *nav) {
    memset(p, 0, sizeof(*p));
    p->base.desc = &g_detail_desc;
    p->nav = nav;
}

void demo_settings_init(demo_settings_page_t *p, shrewd_lv_view_nav_t *nav) {
    memset(p, 0, sizeof(*p));
    p->base.desc = &g_settings_desc;
    p->nav = nav;
}
