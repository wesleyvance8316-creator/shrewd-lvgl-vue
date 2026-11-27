/**
 * @file demo_gauge.h
 * @brief 控件(widget class)样板(design.md §7.3)。
 *
 * 控件是**新的 `lv_obj` 类型**,与原生 widget 同等对待:可放任意容器、走 LVGL
 * property(props)、走 LVGL event。与片段(§7.2)的对照见 §7.1。
 *
 * 这是 facade 的**第三档**(§7.7):要引 lvgl 的**私有头** —— `lv_obj_class_t` 的
 * 完整定义只在 `lvgl_private/` 里(公共树只有前向声明),而那是 **lvgl 的边界,
 * 不是我们的**。
 *
 * 一个水位计:值 0..max,内部一根填充条按比例显示。
 */
#ifndef DEMO_COMPONENTS_DEMO_GAUGE_H
#define DEMO_COMPONENTS_DEMO_GAUGE_H

#include "shrewd-lv-view/view.h"

/* 控件作者要引的私有头(§7.3,照 lvgl 自己 widget 的写法)。 */
#include "lvgl_private/core/lv_obj_class_private.h"
#include "lvgl_private/core/lv_obj_private.h"
#include "lvgl_private/lvgl_public.h"

/**
 * 属性号段**自己划**:`lv_obj_property.h` 明写 0x10000 起留给用户(§7.3)。
 * `LV_PROPERTY_ID` 是**枚举声明**宏,不能写进数组初始化器。
 */
enum {
    LV_PROPERTY_GAUGE_START = 0x10000,
    LV_PROPERTY_ID(GAUGE, VALUE, LV_PROPERTY_TYPE_INT, 0),
    LV_PROPERTY_ID(GAUGE, MAX, LV_PROPERTY_TYPE_INT, 1),
    LV_PROPERTY_GAUGE_END,
};

/**
 * 实例:**`lv_obj_t` 必须是第一个成员** —— LVGL 靠它在 `lv_obj_t *` 和
 * `demo_gauge_t *` 之间做转换。状态放实例里(§7.1),不放外部 struct。
 */
typedef struct {
    lv_obj_t obj;
    int32_t value;
    int32_t max;
    lv_obj_t *fill; /**< 内部填充条 */
} demo_gauge_t;

/** 类描述符:与原生 widget 同一份机制(继承、构造、析构、属性表)。 */
extern const lv_obj_class_t demo_gauge_class;

/** 自定义事件 id(§7.4):值变了就发。先调 `demo_gauge_init()` 注册。 */
extern uint32_t demo_gauge_event_value_changed;

/** 组件库的 init:注册自定义事件 id。也可作为 `shrewd_lv_view_plugin_t` 的 init(§7.7)。 */
int demo_gauge_init(void);

lv_obj_t *demo_gauge_create(lv_obj_t *parent);
void demo_gauge_set_value(lv_obj_t *g, int32_t value);
int32_t demo_gauge_get_value(const lv_obj_t *g);

/**
 * 把 gauge 的 value 接上响应式绑定。
 *
 * **第三方组件库也能扩绑定**(design.md §6 的规则):加一张 target 表 + 一个 3 行
 * 便捷函数,**不用改框架**。所以组件库的产出物不止 widget,还有它的绑定胶水。
 */
int demo_bind_gauge_value(lv_obj_t *g, shrewd_basic_reflect_ref_t *ref);

#endif /* DEMO_COMPONENTS_DEMO_GAUGE_H */
