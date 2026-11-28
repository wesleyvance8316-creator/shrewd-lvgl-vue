/**
 * @file lv_view_bind.c
 * @brief 绑定机制的单点实现(design.md §2 / §3 / §4)。
 *
 * 一条绑定 = 一个 effect:ref 变了自动写 widget;双向时 widget 变了自动
 * 写回 ref。三件事都只在这里实现一次,便捷函数只是薄壳:
 *
 *   - **applying 抑制**(§4):写 widget 期间置位,写回回调看它就退,
 *     否则 ref_set → effect → 写 widget → 事件 → 写回 会成环。
 *   - **DELETE 自清**(§3):绑定记录随 widget 删除而释放,使用者不记不解绑。
 *   - **读回要拷贝**(§5):widget 的读回多是 LVGL 自持指针,写进 ref 槽位
 *     必须拷贝,所以每个绑定带一块 scratch。
 */

#include <string.h>

#include "shrewd-basic/platform/memory.h"
#include "shrewd-lv-view/view.h"

typedef struct shrewd_lv_view_binding {
    shrewd_basic_reflect_effect_t effect;
    shrewd_basic_reflect_ref_t *ref;
    shrewd_lv_view_target_t target; /* 按值存:允许每条绑定自己的 ctx */
    void *obj;
    void *scratch; /**< ref->size bytes, for target->read() */
    int applying;
} shrewd_lv_view_binding_t;

/** ref → widget。由 effect 驱动,ref_set 时同步跑(design.md §2)。 */
static void binding_apply(void *ctx) {
    shrewd_lv_view_binding_t *b = (shrewd_lv_view_binding_t *)ctx;
    const void *value = shrewd_basic_reflect_ref_get(b->ref);
    b->applying = 1;
    b->target.write(b->obj, value, b->ref->size, b->target.ctx);
    b->applying = 0;
}

/** widget → ref。applying 期间退——那是本绑定在写 widget,不是用户改动。 */
static void binding_changed(lv_event_t *e) {
    shrewd_lv_view_binding_t *b = (shrewd_lv_view_binding_t *)lv_event_get_user_data(e);
    if ((0 != b->applying) || (NULL == b->target.read)) {
        return;
    }
    if (SHREWD_LV_VIEW_ERR_OK != b->target.read(b->obj, b->scratch, b->ref->size, b->target.ctx)) {
        return;
    }
    (void)shrewd_basic_reflect_ref_set(b->ref, b->scratch, b->ref->size);
}

/** widget 删除 → 解绑 + 释放。绑定随 widget 死(§3)。 */
static void binding_delete(lv_event_t *e) {
    shrewd_lv_view_binding_t *b = (shrewd_lv_view_binding_t *)lv_event_get_user_data(e);
    shrewd_basic_reflect_effect_dispose(&b->effect);
    void *scratch = b->scratch;
    void *self = b;
    (void)shrewd_basic_platform_free(&scratch);
    (void)shrewd_basic_platform_free(&self);
}

int shrewd_lv_view_bind(void *obj,
                        const shrewd_lv_view_target_t *target,
                        shrewd_basic_reflect_ref_t *ref) {
    if ((NULL == obj) || (NULL == target) || (NULL == target->write) || (NULL == ref)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (0 == ref->size) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    shrewd_lv_view_binding_t *b = NULL;
    void *slot = NULL;
    int status = shrewd_basic_platform_calloc(&slot, 1, sizeof(*b));
    if (SHREWD_BASIC_ERR_OK != status) {
        return status;
    }
    b = (shrewd_lv_view_binding_t *)slot;
    b->ref = ref;
    b->target = *target; /* 按值:调用方可以给栈上的临时 target */
    b->obj = obj;
    status = shrewd_basic_platform_alloc(&b->scratch, ref->size);
    if (SHREWD_BASIC_ERR_OK != status) {
        (void)shrewd_basic_platform_free(&slot);
        return status;
    }
    /* 先挂清理再建 effect:effect_init 立刻跑一次,若中途失败仍可自清。 */
    if (NULL == lv_obj_add_event_cb((lv_obj_t *)obj, binding_delete, LV_EVENT_DELETE, b)) {
        (void)shrewd_basic_platform_free(&b->scratch);
        (void)shrewd_basic_platform_free(&slot);
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    status = shrewd_basic_reflect_effect_init(&b->effect, binding_apply, b);
    if (SHREWD_BASIC_ERR_OK != status) {
        (void)shrewd_basic_platform_free(&b->scratch);
        slot = b;
        (void)shrewd_basic_platform_free(&slot);
        return status;
    }
    if (NULL != target->read) {
        (void)lv_obj_add_event_cb((lv_obj_t *)obj, binding_changed, target->changed_event, b);
    }
    return SHREWD_LV_VIEW_ERR_OK;
}

/* --- 便捷绑定:每个 widget/属性一张 target 表(design.md §6)---------- */

static void label_text_write(void *obj, const void *value, size_t size, void *ctx) {
    (void)size;
    (void)ctx;
    lv_label_set_text((lv_obj_t *)obj, (const char *)value);
}

static const shrewd_lv_view_target_t g_label_text = {
    .write = label_text_write,
    .read = NULL,
    .changed_event = 0,
};

int shrewd_lv_view_bind_label_text(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref) {
    return shrewd_lv_view_bind(obj, &g_label_text, ref);
}

static void slider_value_write(void *obj, const void *value, size_t size, void *ctx) {
    (void)size;
    (void)ctx;
    lv_slider_set_value((lv_obj_t *)obj, *(const int32_t *)value, LV_ANIM_OFF);
}

static int slider_value_read(void *obj, void *out, size_t size, void *ctx) {
    (void)ctx;
    if (sizeof(int32_t) != size) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    *(int32_t *)out = lv_slider_get_value((const lv_obj_t *)obj);
    return SHREWD_LV_VIEW_ERR_OK;
}

static const shrewd_lv_view_target_t g_slider_value = {
    .write = slider_value_write,
    .read = slider_value_read,
    .changed_event = LV_EVENT_VALUE_CHANGED,
};

int shrewd_lv_view_bind_slider_value(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref) {
    return shrewd_lv_view_bind(obj, &g_slider_value, ref);
}

static void textarea_text_write(void *obj, const void *value, size_t size, void *ctx) {
    (void)size;
    (void)ctx;
    lv_textarea_set_text((lv_obj_t *)obj, (const char *)value);
}

static int textarea_text_read(void *obj, void *out, size_t size, void *ctx) {
    (void)ctx;
    const char *text = lv_textarea_get_text((const lv_obj_t *)obj);
    if (NULL == text) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    /* LVGL 自持指针:必须拷贝进槽位(§5)。 */
    (void)memset(out, 0, size);
    (void)strncpy((char *)out, text, size - 1);
    return SHREWD_LV_VIEW_ERR_OK;
}

static const shrewd_lv_view_target_t g_textarea_text = {
    .write = textarea_text_write,
    .read = textarea_text_read,
    .changed_event = LV_EVENT_VALUE_CHANGED,
};

int shrewd_lv_view_bind_textarea_text(lv_obj_t *obj, shrewd_basic_reflect_ref_t *ref) {
    return shrewd_lv_view_bind(obj, &g_textarea_text, ref);
}

/* --- tab 栏:高亮当前分支(architecture.md §6.9)---------------------- */

/**
 * bar 的**直接子项按分支顺序排列**;第 i 个子项在 `*value == i` 时进
 * `LV_STATE_CHECKED`,否则摘掉。**样式交给主题** —— 框架只表达"谁是当前",
 * 不决定它长什么样(design.md §7.6)。
 */
static void tab_bar_write(void *obj, const void *value, size_t size, void *ctx) {
    (void)ctx;
    lv_obj_t *bar = (lv_obj_t *)obj;
    if (sizeof(uint32_t) != size) {
        return;
    }
    uint32_t active = *(const uint32_t *)value;
    uint32_t n = lv_obj_get_child_count(bar);
    for (uint32_t i = 0; i < n; ++i) {
        lv_obj_t *child = lv_obj_get_child(bar, (int32_t)i);
        if (NULL == child) {
            continue;
        }
        if (i == active) {
            lv_obj_add_state(child, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(child, LV_STATE_CHECKED);
        }
    }
}

static const shrewd_lv_view_target_t g_tab_bar = {
    .write = tab_bar_write,
    .read = NULL,
    .changed_event = 0,
};

int shrewd_lv_view_bind_tab_bar(lv_obj_t *bar, shrewd_basic_reflect_ref_t *ref) {
    return shrewd_lv_view_bind(bar, &g_tab_bar, ref);
}

/* --- LVGL property 的通用绑定(§7.4)--------------------------------- */

/**
 * 属性类型 → 槽位宽度。**类型校验就在这一步**:认不出的类型、宽度对不上的
 * ref 都直接拒,绝不带着错的宽度进 setter(那会读越界)。
 */
static int prop_width(uint32_t id, size_t *out) {
    switch (LV_PROPERTY_ID_TYPE(id)) {
    case LV_PROPERTY_TYPE_INT:
    case LV_PROPERTY_TYPE_BOOL:
        *out = sizeof(int32_t);
        return SHREWD_LV_VIEW_ERR_OK;
    case LV_PROPERTY_TYPE_POINTER:
    case LV_PROPERTY_TYPE_TEXT:
    case LV_PROPERTY_TYPE_OBJ:
    case LV_PROPERTY_TYPE_IMGSRC:
    case LV_PROPERTY_TYPE_FONT:
        *out = sizeof(void *);
        return SHREWD_LV_VIEW_ERR_OK;
    case LV_PROPERTY_TYPE_COLOR:
        *out = sizeof(lv_color_t);
        return SHREWD_LV_VIEW_ERR_OK;
    case LV_PROPERTY_TYPE_POINT:
        *out = sizeof(lv_point_t);
        return SHREWD_LV_VIEW_ERR_OK;
    default:
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
}

/* 按**类型**取值,不按宽度 —— lv_color_t 可能和 int32_t 一样宽,按宽度分不清。 */
static void prop_write(void *obj, const void *value, size_t size, void *ctx) {
    uint32_t id = (uint32_t)(uintptr_t)ctx;
    lv_property_t prop;
    memset(&prop, 0, sizeof(prop));
    prop.id = id;
    switch (LV_PROPERTY_ID_TYPE(id)) {
    case LV_PROPERTY_TYPE_INT:
    case LV_PROPERTY_TYPE_BOOL:
        if (sizeof(int32_t) == size) {
            prop.num = *(const int32_t *)value;
        }
        break;
    case LV_PROPERTY_TYPE_POINTER:
    case LV_PROPERTY_TYPE_TEXT:
    case LV_PROPERTY_TYPE_OBJ:
    case LV_PROPERTY_TYPE_IMGSRC:
    case LV_PROPERTY_TYPE_FONT:
        if (sizeof(void *) == size) {
            prop.ptr = *(void *const *)value;
        }
        break;
    case LV_PROPERTY_TYPE_COLOR:
        if (sizeof(lv_color_t) == size) {
            prop.color = *(const lv_color_t *)value;
        }
        break;
    case LV_PROPERTY_TYPE_POINT:
        if (sizeof(lv_point_t) == size) {
            prop.point = *(const lv_point_t *)value;
        }
        break;
    default:
        return;
    }
    (void)lv_obj_set_property((lv_obj_t *)obj, &prop);
}

static int prop_read(void *obj, void *out, size_t size, void *ctx) {
    uint32_t id = (uint32_t)(uintptr_t)ctx;
    lv_property_t prop = lv_obj_get_property((lv_obj_t *)obj, id);
    switch (LV_PROPERTY_ID_TYPE(id)) {
    case LV_PROPERTY_TYPE_INT:
    case LV_PROPERTY_TYPE_BOOL:
        if (sizeof(int32_t) == size) {
            *(int32_t *)out = prop.num;
            return SHREWD_LV_VIEW_ERR_OK;
        }
        break;
    case LV_PROPERTY_TYPE_POINTER:
    case LV_PROPERTY_TYPE_TEXT:
    case LV_PROPERTY_TYPE_OBJ:
    case LV_PROPERTY_TYPE_IMGSRC:
    case LV_PROPERTY_TYPE_FONT:
        if (sizeof(void *) == size) {
            *(const void **)out = prop.ptr;
            return SHREWD_LV_VIEW_ERR_OK;
        }
        break;
    case LV_PROPERTY_TYPE_COLOR:
        if (sizeof(lv_color_t) == size) {
            *(lv_color_t *)out = prop.color;
            return SHREWD_LV_VIEW_ERR_OK;
        }
        break;
    case LV_PROPERTY_TYPE_POINT:
        if (sizeof(lv_point_t) == size) {
            *(lv_point_t *)out = prop.point;
            return SHREWD_LV_VIEW_ERR_OK;
        }
        break;
    default:
        break;
    }
    return SHREWD_LV_VIEW_ERR_ARG_INVALID;
}

int shrewd_lv_view_bind_prop(lv_obj_t *obj,
                             uint32_t id,
                             shrewd_basic_reflect_ref_t *ref,
                             uint32_t changed_event) {
    if ((NULL == obj) || (NULL == ref)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    size_t want = 0;
    if (SHREWD_LV_VIEW_ERR_OK != prop_width(id, &want)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (want != ref->size) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID; /* 类型不匹配:拒,不进 setter */
    }
    shrewd_lv_view_target_t target;
    memset(&target, 0, sizeof(target));
    target.write = prop_write;
    target.read = (0 != changed_event) ? prop_read : NULL;
    target.changed_event = changed_event;
    target.ctx = (void *)(uintptr_t)id;
    return shrewd_lv_view_bind(obj, &target, ref);
}
