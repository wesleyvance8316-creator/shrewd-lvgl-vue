/**
 * @file lv_view_nav.c
 * @brief 导航栈(architecture.md §6.6–6.7 / design.md §8.4–8.5)。
 *
 * 栈条目是**描述**,不持 widget;**栈深 ≠ 实例数** —— widget 树由释放策略
 * 独立决定何时建/销(§6.3)。四阶段钩子(创建/激活/失活/销毁)由这里驱动,
 * 顺序确定、一定被调用(§6.2)。
 */

#include <string.h>

#include "shrewd-basic/platform/memory.h"
#include "shrewd-lv-view/view.h"

#define NAV_MIN_CAPACITY 8

/* --- 栈条目:widget 树的建 / 销 ------------------------------------- */

/** 销毁 widget 树。绑定随 widget 级联自清(design.md §3)。 */
static void entry_destroy(shrewd_lv_view_nav_entry_t *e) {
    if (0 == e->built) {
        return;
    }
    if (NULL != e->page->desc->destroy) {
        e->page->desc->destroy(e->page);
    }
    if (NULL != e->page->root) {
        lv_obj_delete(e->page->root);
        e->page->root = NULL;
    }
    e->built = 0;
}

/** 让条目成为活动页:创建(若未建)+ 激活。 */
static int entry_activate(shrewd_lv_view_nav_t *nav, shrewd_lv_view_nav_entry_t *e) {
    if (0 == e->built) {
        if (NULL == e->page->desc->create) {
            return SHREWD_LV_VIEW_ERR_ARG_INVALID;
        }
        int status = e->page->desc->create(e->page, nav->host, e->params);
        if (SHREWD_LV_VIEW_ERR_OK != status) {
            return status;
        }
        e->built = 1;
    }
    if (NULL != e->page->desc->enter) {
        return e->page->desc->enter(e->page);
    }
    return SHREWD_LV_VIEW_ERR_OK;
}

/** 让条目失活,并按释放策略决定要不要销毁 widget 树。 */
static void entry_deactivate(shrewd_lv_view_nav_entry_t *e) {
    if (NULL != e->page->desc->exit) {
        e->page->desc->exit(e->page);
    }
    if (SHREWD_LV_VIEW_RELEASE_DESTROY_ON_EXIT == e->page->desc->release) {
        entry_destroy(e);
    }
}

/**
 * 缓存预算:RELEASE_CACHE 的页面最多留 cache_count 个已建的 widget 树,
 * 超出按"最久未激活"淘汰(architecture.md §6.5)。
 */
static void enforce_cache(shrewd_lv_view_nav_t *nav) {
    size_t budget = 0;
    size_t built = 0;
    for (size_t i = 0; i < nav->count; ++i) {
        if (0 != nav->entries[i].built) {
            built += 1;
            if (SHREWD_LV_VIEW_RELEASE_CACHE == nav->entries[i].page->desc->release) {
                budget += nav->entries[i].page->desc->cache_count;
            }
        }
    }
    while (built > budget) {
        shrewd_lv_view_nav_entry_t *oldest = NULL;
        for (size_t i = 0; i < nav->count; ++i) {
            if ((0 != nav->entries[i].built)
                && (SHREWD_LV_VIEW_RELEASE_CACHE == nav->entries[i].page->desc->release)
                && ((NULL == oldest) || (nav->entries[i].entered_at < oldest->entered_at))) {
                oldest = &nav->entries[i];
            }
        }
        if (NULL == oldest) {
            break;
        }
        entry_destroy(oldest);
        built -= 1;
    }
}

/* --- 栈容器 ---------------------------------------------------------- */

static int nav_reserve(shrewd_lv_view_nav_t *nav, size_t want) {
    if (want <= nav->capacity) {
        return SHREWD_LV_VIEW_ERR_OK;
    }
    size_t cap = (0 == nav->capacity) ? NAV_MIN_CAPACITY : nav->capacity;
    while (cap < want) {
        cap *= 2;
    }
    void *slot = NULL;
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_platform_realloc(&slot, nav->entries, cap * sizeof(*nav->entries))) {
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    nav->entries = (shrewd_lv_view_nav_entry_t *)slot;
    nav->capacity = cap;
    return SHREWD_LV_VIEW_ERR_OK;
}

static shrewd_lv_view_nav_entry_t *nav_top_entry(shrewd_lv_view_nav_t *nav) {
    return (0 < nav->count) ? &nav->entries[nav->count - 1] : NULL;
}

/** 走守卫。返回 GUARD_* 的判定,或 <0 表示守卫无法完成。 */
static shrewd_lv_view_guard_result_t nav_check(shrewd_lv_view_nav_t *nav,
                                               shrewd_lv_view_page_t *to,
                                               shrewd_lv_view_page_t **redirect) {
    *redirect = NULL;
    if (NULL == nav->guard) {
        return SHREWD_LV_VIEW_GUARD_ALLOW;
    }
    shrewd_lv_view_nav_request_t req;
    memset(&req, 0, sizeof(req));
    req.to = (NULL != to) ? to->desc : NULL;
    req.from = (NULL != nav_top_entry(nav)) ? nav_top_entry(nav)->page->desc : NULL;
    shrewd_lv_view_guard_result_t verdict = nav->guard(nav->guard_ctx, &req);
    *redirect = req.redirect;
    return verdict;
}

static int params_copy(const shrewd_lv_view_page_desc_t *desc,
                       const void *params,
                       void **out,
                       size_t *out_size) {
    *out = NULL;
    *out_size = 0;
    if ((NULL == params) || (NULL == desc->param_type)) {
        return SHREWD_LV_VIEW_ERR_OK;
    }
    size_t size = desc->param_type->sb_refl_size;
    if (0 == size) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    void *slot = NULL;
    if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&slot, size)) {
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    memcpy(slot, params, size);
    *out = slot;
    *out_size = size;
    return SHREWD_LV_VIEW_ERR_OK;
}

int shrewd_lv_view_nav_init(shrewd_lv_view_nav_t *nav, lv_obj_t *host, size_t limit) {
    if ((NULL == nav) || (NULL == host)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    memset(nav, 0, sizeof(*nav));
    nav->host = host;
    nav->limit = (0 == limit) ? SHREWD_LV_VIEW_NAV_DEFAULT_LIMIT : limit;
    nav->next_seq = 1;
    return SHREWD_LV_VIEW_ERR_OK;
}

void shrewd_lv_view_nav_dispose(shrewd_lv_view_nav_t *nav) {
    if (NULL == nav) {
        return;
    }
    for (size_t i = 0; i < nav->count; ++i) {
        entry_destroy(&nav->entries[i]);
        if (NULL != nav->entries[i].params) {
            void *p = nav->entries[i].params;
            (void)shrewd_basic_platform_free(&p);
        }
    }
    if (NULL != nav->entries) {
        void *p = nav->entries;
        (void)shrewd_basic_platform_free(&p);
    }
    memset(nav, 0, sizeof(*nav));
}

void shrewd_lv_view_nav_set_guard(shrewd_lv_view_nav_t *nav,
                                  shrewd_lv_view_guard_fn guard,
                                  void *ctx) {
    if (NULL == nav) {
        return;
    }
    nav->guard = guard;
    nav->guard_ctx = ctx;
}

size_t shrewd_lv_view_nav_depth(const shrewd_lv_view_nav_t *nav) {
    return (NULL != nav) ? nav->count : 0;
}

shrewd_lv_view_page_t *shrewd_lv_view_nav_top(shrewd_lv_view_nav_t *nav) {
    shrewd_lv_view_nav_entry_t *e = nav_top_entry(nav);
    return (NULL != e) ? e->page : NULL;
}

int shrewd_lv_view_nav_push(shrewd_lv_view_nav_t *nav,
                            shrewd_lv_view_page_t *page,
                            const void *params) {
    if ((NULL == nav) || (NULL == page) || (NULL == page->desc)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (nav->count >= nav->limit) {
        return SHREWD_LV_VIEW_ERR_NAV_STACK_FULL;
    }
    shrewd_lv_view_page_t *redirect = NULL;
    shrewd_lv_view_guard_result_t verdict = nav_check(nav, page, &redirect);
    if (SHREWD_LV_VIEW_GUARD_CANCEL == verdict) {
        return SHREWD_LV_VIEW_ERR_NAV_CANCELLED;
    }
    if (SHREWD_LV_VIEW_GUARD_CONSUME == verdict) {
        return SHREWD_LV_VIEW_ERR_OK;
    }
    if (SHREWD_LV_VIEW_GUARD_REDIRECT == verdict) {
        page = (NULL != redirect) ? redirect : page;
    }
    if (SHREWD_LV_VIEW_ERR_OK != nav_reserve(nav, nav->count + 1)) {
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    shrewd_lv_view_nav_entry_t *e = &nav->entries[nav->count];
    memset(e, 0, sizeof(*e));
    e->page = page;
    e->seq = nav->next_seq;
    nav->next_seq += 1;
    e->entered_at = (uint64_t)lv_tick_get();
    int status = params_copy(page->desc, params, &e->params, &e->params_size);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    shrewd_lv_view_nav_entry_t *prev = nav_top_entry(nav);
    if (NULL != prev) {
        entry_deactivate(prev);
    }
    nav->count += 1;
    status = entry_activate(nav, e);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    enforce_cache(nav);
    return SHREWD_LV_VIEW_ERR_OK;
}

int shrewd_lv_view_nav_pop(shrewd_lv_view_nav_t *nav) {
    if (NULL == nav) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    shrewd_lv_view_nav_entry_t *top = nav_top_entry(nav);
    if (NULL == top) {
        return SHREWD_LV_VIEW_ERR_NAV_EMPTY;
    }
    shrewd_lv_view_page_t *redirect = NULL;
    shrewd_lv_view_guard_result_t verdict = nav_check(nav, NULL, &redirect);
    if (SHREWD_LV_VIEW_GUARD_CANCEL == verdict) {
        return SHREWD_LV_VIEW_ERR_NAV_CANCELLED;
    }
    if (SHREWD_LV_VIEW_GUARD_CONSUME == verdict) {
        return SHREWD_LV_VIEW_ERR_OK;
    }
    entry_deactivate(top);
    if (NULL != top->params) {
        void *p = top->params;
        (void)shrewd_basic_platform_free(&p);
    }
    nav->count -= 1;
    shrewd_lv_view_nav_entry_t *now = nav_top_entry(nav);
    if (NULL != now) {
        return entry_activate(nav, now);
    }
    return SHREWD_LV_VIEW_ERR_OK;
}

int shrewd_lv_view_nav_pop_until(shrewd_lv_view_nav_t *nav,
                                 shrewd_lv_view_nav_pred_fn pred,
                                 void *ctx) {
    if ((NULL == nav) || (NULL == pred)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    while (0 < nav->count) {
        shrewd_lv_view_nav_entry_t *top = nav_top_entry(nav);
        if (0 != pred(ctx, top->page)) {
            /* 命中即停。新栈顶的激活由 nav_pop 做过,这里不能再来一次。 */
            return SHREWD_LV_VIEW_ERR_OK;
        }
        int status = shrewd_lv_view_nav_pop(nav);
        if (SHREWD_LV_VIEW_ERR_OK != status) {
            return status;
        }
    }
    return SHREWD_LV_VIEW_ERR_NAV_EMPTY;
}

int shrewd_lv_view_nav_replace(shrewd_lv_view_nav_t *nav,
                               shrewd_lv_view_page_t *page,
                               const void *params) {
    if ((NULL == nav) || (NULL == page) || (NULL == page->desc)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    shrewd_lv_view_page_t *redirect = NULL;
    shrewd_lv_view_guard_result_t verdict = nav_check(nav, page, &redirect);
    if (SHREWD_LV_VIEW_GUARD_CANCEL == verdict) {
        return SHREWD_LV_VIEW_ERR_NAV_CANCELLED;
    }
    if (SHREWD_LV_VIEW_GUARD_CONSUME == verdict) {
        return SHREWD_LV_VIEW_ERR_OK;
    }
    if (SHREWD_LV_VIEW_GUARD_REDIRECT == verdict) {
        page = (NULL != redirect) ? redirect : page;
    }
    shrewd_lv_view_nav_entry_t *top = nav_top_entry(nav);
    if (NULL == top) {
        /* 空栈上 replace 等价于 push,但不产生历史。 */
        return shrewd_lv_view_nav_push(nav, page, params);
    }
    if (top->page == page) {
        /* 同页:只换参数,不重建。 */
        void *copy = NULL;
        size_t size = 0;
        int status = params_copy(page->desc, params, &copy, &size);
        if (SHREWD_LV_VIEW_ERR_OK != status) {
            return status;
        }
        if (NULL != top->params) {
            void *p = top->params;
            (void)shrewd_basic_platform_free(&p);
        }
        top->params = copy;
        top->params_size = size;
        return SHREWD_LV_VIEW_ERR_OK;
    }
    entry_deactivate(top);
    if (NULL != top->params) {
        void *p = top->params;
        (void)shrewd_basic_platform_free(&p);
    }
    memset(top, 0, sizeof(*top));
    top->page = page;
    top->seq = nav->next_seq;
    nav->next_seq += 1;
    top->entered_at = (uint64_t)lv_tick_get();
    int status = params_copy(page->desc, params, &top->params, &top->params_size);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    status = entry_activate(nav, top);
    enforce_cache(nav);
    return status;
}
