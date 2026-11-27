/**
 * @file demo_gauge.c
 * @brief 控件(widget class)样板的实现(design.md §7.3)。
 *
 * 照 lvgl 自己 widget 的写法:类描述符 + 属性表 + 自定义事件 id。
 */
#include "demo_gauge.h"

uint32_t demo_gauge_event_value_changed;

int demo_gauge_init(void) {
    if (0 == demo_gauge_event_value_changed) {
        demo_gauge_event_value_changed = lv_event_register_id();
    }
    return SHREWD_LV_VIEW_ERR_OK;
}

/* --- 类:构造 / 析构 ----------------------------------------------- */

static void demo_gauge_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj) {
    (void)class_p;
    /* 先跑父类构造,再建自己的子 widget。 */
    demo_gauge_t *g = (demo_gauge_t *)obj;
    g->value = 0;
    g->max = 100;
    g->fill = lv_obj_create(obj);
    lv_obj_set_size(g->fill, 4, 10);
    lv_obj_align(g->fill, LV_ALIGN_LEFT_MID, 0, 0);
}

static void demo_gauge_destructor(const lv_obj_class_t *class_p, lv_obj_t *obj) {
    (void)class_p;
    (void)obj;
    /* 子 widget 随父删除,绑定也级联自清(§3),这里什么都不用做。 */
}

/* --- 属性:LVGL property(props,§7.4)----------------------------- */

void demo_gauge_set_value(lv_obj_t *obj, int32_t value) {
    demo_gauge_t *g = (demo_gauge_t *)obj;
    if (0 > value) {
        value = 0;
    }
    if (value > g->max) {
        value = g->max;
    }
    if (value == g->value) {
        return;
    }
    g->value = value;
    /* 填充条按比例 —— 组件内部自己画,这是控件的自由。 */
    int32_t w = lv_obj_get_width(obj);
    lv_obj_set_width(g->fill, (0 < g->max) ? ((w * value) / g->max) : 0);
    if (0 != demo_gauge_event_value_changed) {
        lv_obj_send_event(obj, demo_gauge_event_value_changed, NULL);
    }
}

int32_t demo_gauge_get_value(const lv_obj_t *obj) {
    return ((const demo_gauge_t *)obj)->value;
}

/*
 * 属性表**必须叫 `lv_<widget>_properties`** —— `LV_PROPERTY_CLASS_FIELDS`
 * 就是这么找的(§7.3 的坑之一)。widget 取 gauge,所以是 lv_gauge_properties。
 *
 * `lv_property_ops_t` 的 setter/getter 是 `void *`,而 **ISO C 不许函数指针与
 * 对象指针互转**(POSIX 才允许),`-Wpedantic` 必叫。lvgl 自己的 widget 也这么写,
 * 所以这是第三档作者都会撞的一处,就地压掉而不是改签名。
 */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
static const lv_property_ops_t lv_gauge_properties[] = {
    {
        .id = LV_PROPERTY_GAUGE_VALUE,
        .setter = demo_gauge_set_value,
        .getter = demo_gauge_get_value,
    },
    {
        .id = LV_PROPERTY_GAUGE_MAX,
        .setter = NULL,
        .getter = NULL,
    },
};
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

const lv_obj_class_t demo_gauge_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = demo_gauge_constructor,
    .destructor_cb = demo_gauge_destructor,
    .instance_size = sizeof(demo_gauge_t),
    .name = "demo_gauge",
    .theme_inheritable = 1, /* 走 LVGL 主题(§7.6),不自造 */
    LV_PROPERTY_CLASS_FIELDS(gauge, GAUGE),
};

lv_obj_t *demo_gauge_create(lv_obj_t *parent) {
    /* **两步**:`create_obj` 只分配,`init_obj` 才跑构造 + 套主题(§7.3)。
       少了第二步,constructor 不跑、属性全落在零值上 —— 实测 8 条断言齐红。 */
    lv_obj_t *obj = lv_obj_class_create_obj(&demo_gauge_class, parent);
    if (NULL == obj) {
        return NULL;
    }
    lv_obj_class_init_obj(obj);
    return obj;
}
