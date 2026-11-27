/**
 * @file lv_view_nav.c
 * @brief 导航:分层的栈(architecture.md §6.6–6.9 / design.md §8)。
 *
 * 栈条目是**描述**,不持 widget;**栈深 ≠ 实例数** —— widget 树由释放策略
 * 独立决定何时建/销(§6.3)。四阶段钩子(创建/激活/失活/销毁)由这里驱动,
 * 顺序确定、一定被调用(§6.2)。
 *
 * 结构是**分层的栈**(§6.9):分支各持独立栈(tab 切换不产生历史),
 * 模态是独立一层、叠在 LVGL top layer 上。操作集只有一套,作用在**当前栈**:
 * 模态栈非空时是它,否则是当前分支 —— 模态因此天然叠在最上、还能嵌套。
 */

#include <string.h>

#include "shrewd-basic/platform/memory.h"
#include "shrewd-lv-view/view.h"

#define NAV_MIN_CAPACITY 8

/* --- widget 树的建 / 销 --------------------------------------------- */

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
static int entry_activate(lv_obj_t *host, shrewd_lv_view_nav_entry_t *e) {
    if (0 == e->built) {
        if (NULL == e->page->desc->create) {
            return SHREWD_LV_VIEW_ERR_ARG_INVALID;
        }
        int status = e->page->desc->create(e->page, host, e->params);
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

static void entry_free_params(shrewd_lv_view_nav_entry_t *e) {
    if (NULL != e->params) {
        void *p = e->params;
        (void)shrewd_basic_platform_free(&p);
        e->params = NULL;
        e->params_size = 0;
    }
}

/**
 * 清掉一个**已失活**的条目:销毁 widget 树 + 释放参数副本。
 *
 * 不再走 exit —— 它在被盖住的那一刻已经 exit 过(§6.2 顺序确定,一次不多
 * 一次不少)。用在 `remove_until` 的下层清理:那些层早已不是活动页,直接
 * 出栈就得连壳带参数一起还掉。
 */
static void entry_discard(shrewd_lv_view_nav_entry_t *e) {
    entry_destroy(e);
    entry_free_params(e);
}

/** 缓存预算:RELEASE_CACHE 最多留 cache_count 个已建的,超出按最久未激活淘汰。 */
static void enforce_cache(shrewd_lv_view_nav_t *nav) {
    size_t budget = 0;
    size_t built = 0;
    shrewd_lv_view_nav_stack_t *stacks[SHREWD_LV_VIEW_NAV_BRANCHES + 1];
    size_t nstacks = 0;
    for (size_t i = 0; i < nav->branch_count; ++i) {
        stacks[nstacks++] = &nav->branches[i];
    }
    stacks[nstacks++] = &nav->modal;
    for (size_t s = 0; s < nstacks; ++s) {
        for (size_t i = 0; i < stacks[s]->count; ++i) {
            shrewd_lv_view_nav_entry_t *e = &stacks[s]->entries[i];
            if (0 == e->built) {
                continue;
            }
            built += 1;
            if (SHREWD_LV_VIEW_RELEASE_CACHE == e->page->desc->release) {
                budget += e->page->desc->cache_count;
            }
        }
    }
    while (built > budget) {
        shrewd_lv_view_nav_entry_t *oldest = NULL;
        for (size_t s = 0; s < nstacks; ++s) {
            for (size_t i = 0; i < stacks[s]->count; ++i) {
                shrewd_lv_view_nav_entry_t *e = &stacks[s]->entries[i];
                if ((0 != e->built) && (SHREWD_LV_VIEW_RELEASE_CACHE == e->page->desc->release)
                    && ((NULL == oldest) || (e->entered_at < oldest->entered_at))) {
                    oldest = e;
                }
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

static int stack_reserve(shrewd_lv_view_nav_stack_t *stack, size_t want) {
    if (want <= stack->capacity) {
        return SHREWD_LV_VIEW_ERR_OK;
    }
    size_t cap = (0 == stack->capacity) ? NAV_MIN_CAPACITY : stack->capacity;
    while (cap < want) {
        cap *= 2;
    }
    void *slot = NULL;
    if (SHREWD_BASIC_ERR_OK
        != shrewd_basic_platform_realloc(&slot, stack->entries, cap * sizeof(*stack->entries))) {
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    stack->entries = (shrewd_lv_view_nav_entry_t *)slot;
    stack->capacity = cap;
    return SHREWD_LV_VIEW_ERR_OK;
}

static shrewd_lv_view_nav_entry_t *stack_top(shrewd_lv_view_nav_stack_t *stack) {
    return (0 < stack->count) ? &stack->entries[stack->count - 1] : NULL;
}

/** 当前栈:模态非空时是它,否则当前分支(§6.9)。 */
static shrewd_lv_view_nav_stack_t *nav_active(shrewd_lv_view_nav_t *nav) {
    return (0 < nav->modal.count) ? &nav->modal : &nav->branches[nav->current];
}

static lv_obj_t *nav_active_host(shrewd_lv_view_nav_t *nav) {
    return (0 < nav->modal.count) ? nav->modal_host : nav->host;
}

/** 清空一个栈:逐个失活 + 按策略销毁 + 释放参数副本。 */
static void stack_clear(shrewd_lv_view_nav_stack_t *stack) {
    while (0 < stack->count) {
        shrewd_lv_view_nav_entry_t *top = stack_top(stack);
        entry_deactivate(top);
        entry_free_params(top);
        stack->count -= 1;
    }
}

/* --- 公共小工具 ------------------------------------------------------ */

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
    shrewd_lv_view_nav_entry_t *top = stack_top(nav_active(nav));
    req.from = (NULL != top) ? top->page->desc : NULL;
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

/** 导航后刷新可绑定的导航状态(定义在 nav_init 之后)。 */
static void nav_publish(shrewd_lv_view_nav_t *nav);

/** 往栈里压一条并激活它(旧栈顶先失活)。 */
static int stack_push(shrewd_lv_view_nav_t *nav,
                      shrewd_lv_view_nav_stack_t *stack,
                      lv_obj_t *host,
                      shrewd_lv_view_page_t *page,
                      const void *params) {
    if (stack->count >= nav->limit) {
        return SHREWD_LV_VIEW_ERR_NAV_STACK_FULL;
    }
    if (SHREWD_LV_VIEW_ERR_OK != stack_reserve(stack, stack->count + 1)) {
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    shrewd_lv_view_nav_entry_t *e = &stack->entries[stack->count];
    memset(e, 0, sizeof(*e));
    e->page = page;
    e->seq = nav->next_seq;
    nav->next_seq += 1;
    e->entered_at = (uint64_t)lv_tick_get();
    int status = params_copy(page->desc, params, &e->params, &e->params_size);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    shrewd_lv_view_nav_entry_t *prev = stack_top(stack);
    if (NULL != prev) {
        entry_deactivate(prev);
    }
    stack->count += 1;
    status = entry_activate(host, e);
    nav_publish(nav);
    return status;
}

/* --- 生命周期 -------------------------------------------------------- */

int shrewd_lv_view_nav_init(shrewd_lv_view_nav_t *nav,
                            lv_obj_t *host,
                            lv_obj_t *modal_host,
                            size_t limit) {
    if ((NULL == nav) || (NULL == host)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    memset(nav, 0, sizeof(*nav));
    nav->host = host;
    nav->modal_host = modal_host;
    nav->limit = (0 == limit) ? SHREWD_LV_VIEW_NAV_DEFAULT_LIMIT : limit;
    nav->next_seq = 1;
    nav->branch_count = 1;
    nav->current = 0;
    /* 可绑定的导航状态(§6.4):壳的 UI 是它们的绑定。 */
    (void)shrewd_basic_reflect_ref_init(
        &nav->current_name, nav->current_name_buf, sizeof(nav->current_name_buf));
    (void)shrewd_basic_reflect_ref_init(
        &nav->current_branch, &nav->current_branch_val, sizeof(nav->current_branch_val));
    return SHREWD_LV_VIEW_ERR_OK;
}

/** 导航后刷新可绑定的导航状态(标题栏、底栏靠它自动跟)。 */
static void nav_publish(shrewd_lv_view_nav_t *nav) {
    shrewd_lv_view_page_t *top = shrewd_lv_view_nav_top(nav);
    const char *name = ((NULL != top) && (NULL != top->desc) && (NULL != top->desc->name))
                           ? top->desc->name
                           : "";
    (void)memset(nav->current_name_buf, 0, sizeof(nav->current_name_buf));
    (void)strncpy(nav->current_name_buf, name, sizeof(nav->current_name_buf) - 1);
    (void)shrewd_basic_reflect_ref_set(
        &nav->current_name, nav->current_name_buf, sizeof(nav->current_name_buf));
    nav->current_branch_val = (uint32_t)nav->current;
    (void)shrewd_basic_reflect_ref_set(
        &nav->current_branch, &nav->current_branch_val, sizeof(nav->current_branch_val));
}

void shrewd_lv_view_nav_dispose(shrewd_lv_view_nav_t *nav) {
    if (NULL == nav) {
        return;
    }
    for (size_t i = 0; i < nav->branch_count; ++i) {
        stack_clear(&nav->branches[i]);
        if (NULL != nav->branches[i].entries) {
            void *p = nav->branches[i].entries;
            (void)shrewd_basic_platform_free(&p);
        }
    }
    stack_clear(&nav->modal);
    if (NULL != nav->modal.entries) {
        void *p = nav->modal.entries;
        (void)shrewd_basic_platform_free(&p);
    }
    if (NULL != nav->registry) {
        void *p = nav->registry;
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

/* --- 登记表:name -> 实例(只服务恢复,§8.6)------------------------- */

int shrewd_lv_view_nav_register(shrewd_lv_view_nav_t *nav, shrewd_lv_view_page_t *page) {
    if ((NULL == nav) || (NULL == page) || (NULL == page->desc) || (NULL == page->desc->name)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < nav->registry_count; ++i) {
        if (nav->registry[i] == page) {
            return SHREWD_LV_VIEW_ERR_OK; /* 幂等 */
        }
    }
    if (nav->registry_count >= nav->registry_capacity) {
        size_t cap = (0 == nav->registry_capacity) ? NAV_MIN_CAPACITY : nav->registry_capacity * 2;
        void *slot = NULL;
        if (SHREWD_BASIC_ERR_OK
            != shrewd_basic_platform_realloc(&slot, nav->registry, cap * sizeof(*nav->registry))) {
            return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
        }
        nav->registry = (shrewd_lv_view_page_t **)slot;
        nav->registry_capacity = cap;
    }
    nav->registry[nav->registry_count] = page;
    nav->registry_count += 1;
    return SHREWD_LV_VIEW_ERR_OK;
}

static shrewd_lv_view_page_t *nav_find_by_name(shrewd_lv_view_nav_t *nav, const char *name) {
    for (size_t i = 0; i < nav->registry_count; ++i) {
        if ((NULL != nav->registry[i]->desc) && (NULL != nav->registry[i]->desc->name)
            && (0 == strcmp(nav->registry[i]->desc->name, name))) {
            return nav->registry[i];
        }
    }
    return NULL;
}

/* --- 操作集:作用在当前栈 ------------------------------------------- */

size_t shrewd_lv_view_nav_depth(const shrewd_lv_view_nav_t *nav) {
    if (NULL == nav) {
        return 0;
    }
    return (0 < nav->modal.count) ? nav->modal.count : nav->branches[nav->current].count;
}

int shrewd_lv_view_nav_in_modal(const shrewd_lv_view_nav_t *nav) {
    return (NULL != nav) && (0 < nav->modal.count);
}

shrewd_lv_view_page_t *shrewd_lv_view_nav_top(shrewd_lv_view_nav_t *nav) {
    if (NULL == nav) {
        return NULL;
    }
    shrewd_lv_view_nav_entry_t *e = stack_top(nav_active(nav));
    return (NULL != e) ? e->page : NULL;
}

int shrewd_lv_view_nav_switch_branch(shrewd_lv_view_nav_t *nav, size_t branch) {
    if ((NULL == nav) || (SHREWD_LV_VIEW_NAV_BRANCHES <= branch)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (0 < nav->modal.count) {
        return SHREWD_LV_VIEW_ERR_FULL; /* 模态压着时不能换分支 */
    }
    if (branch == nav->current) {
        return SHREWD_LV_VIEW_ERR_OK;
    }
    /* 换分支 = 旧分支失活,新分支激活。**不产生历史**(§6.9)。 */
    shrewd_lv_view_nav_entry_t *old = stack_top(&nav->branches[nav->current]);
    if (branch >= nav->branch_count) {
        nav->branch_count = branch + 1;
    }
    nav->current = branch;
    if (NULL != old) {
        entry_deactivate(old);
    }
    shrewd_lv_view_nav_entry_t *now = stack_top(&nav->branches[nav->current]);
    int status = (NULL != now) ? entry_activate(nav->host, now) : SHREWD_LV_VIEW_ERR_OK;
    nav_publish(nav);
    return status;
}

int shrewd_lv_view_nav_push(shrewd_lv_view_nav_t *nav,
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
    /* push 顺带登记:让 name -> 实例 可查,恢复就不用额外记账。 */
    (void)shrewd_lv_view_nav_register(nav, page);
    int status = stack_push(nav, nav_active(nav), nav_active_host(nav), page, params);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    enforce_cache(nav);
    return SHREWD_LV_VIEW_ERR_OK;
}

int shrewd_lv_view_nav_modal_push(shrewd_lv_view_nav_t *nav,
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
    (void)shrewd_lv_view_nav_register(nav, page);
    /* 显式压进模态栈 —— 不管当前栈是什么。 */
    int status = stack_push(nav, &nav->modal, nav->modal_host, page, params);
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
    shrewd_lv_view_nav_stack_t *stack = nav_active(nav);
    shrewd_lv_view_nav_entry_t *top = stack_top(stack);
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
    entry_free_params(top);
    stack->count -= 1;
    shrewd_lv_view_nav_entry_t *now = stack_top(stack);
    int status = (NULL != now) ? entry_activate(nav_active_host(nav), now) : SHREWD_LV_VIEW_ERR_OK;
    nav_publish(nav);
    return status;
}

int shrewd_lv_view_nav_pop_until(shrewd_lv_view_nav_t *nav,
                                 shrewd_lv_view_nav_pred_fn pred,
                                 void *ctx) {
    if ((NULL == nav) || (NULL == pred)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    while (0 < shrewd_lv_view_nav_depth(nav)) {
        shrewd_lv_view_nav_entry_t *top = stack_top(nav_active(nav));
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

int shrewd_lv_view_nav_remove_until(shrewd_lv_view_nav_t *nav,
                                    shrewd_lv_view_page_t *page,
                                    const void *params,
                                    shrewd_lv_view_nav_pred_fn pred,
                                    void *ctx) {
    if ((NULL == nav) || (NULL == page) || (NULL == page->desc)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    /* 这是"离开当前页 + 去新页",守卫同 push。 */
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
    (void)shrewd_lv_view_nav_register(nav, page);
    shrewd_lv_view_nav_stack_t *stack = nav_active(nav);
    int status = stack_push(nav, stack, nav_active_host(nav), page, params);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    /* 清下层:新栈顶以下,**谓词不命中的逐个清掉**,第一个命中的留下
       (连同它以下全部)。pred == NULL 恒不命中 —— 锚点以下全清。
       每次只清"倒数第二",所以它上面只有新栈顶,挪下来补位即可。 */
    while (1 < stack->count) {
        shrewd_lv_view_nav_entry_t *e = &stack->entries[stack->count - 2];
        if ((NULL != pred) && (0 != pred(ctx, e->page))) {
            break;
        }
        entry_discard(e);
        stack->entries[stack->count - 2] = stack->entries[stack->count - 1];
        stack->count -= 1;
    }
    enforce_cache(nav);
    return SHREWD_LV_VIEW_ERR_OK;
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
    (void)shrewd_lv_view_nav_register(nav, page);
    shrewd_lv_view_nav_stack_t *stack = nav_active(nav);
    shrewd_lv_view_nav_entry_t *top = stack_top(stack);
    if (NULL == top) {
        return stack_push(nav, stack, nav_active_host(nav), page, params);
    }
    if (top->page == page) {
        void *copy = NULL;
        size_t size = 0;
        int status = params_copy(page->desc, params, &copy, &size);
        if (SHREWD_LV_VIEW_ERR_OK != status) {
            return status;
        }
        entry_free_params(top);
        top->params = copy;
        top->params_size = size;
        return SHREWD_LV_VIEW_ERR_OK;
    }
    entry_deactivate(top);
    entry_free_params(top);
    memset(top, 0, sizeof(*top));
    top->page = page;
    top->seq = nav->next_seq;
    nav->next_seq += 1;
    top->entered_at = (uint64_t)lv_tick_get();
    int status = params_copy(page->desc, params, &top->params, &top->params_size);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    status = entry_activate(nav_active_host(nav), top);
    nav_publish(nav);
    enforce_cache(nav);
    return status;
}

/* --- 栈快照(§8.6)-------------------------------------------------
 *
 * 小端显式,不依赖宿主字节序:
 *   u32 magic 'SLVN' | u32 version | u32 branch_count | u32 current
 *   每个分支: u32 count + 条目 × count
 *   模态:     u32 count + 条目 × count
 *   条目:     u32 seq | u32 name_len | u32 params_size | name | params
 */

#define NAV_SNAPSHOT_MAGIC 0x4E564C53u
#define NAV_SNAPSHOT_VERSION 1u

static void put_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static uint32_t get_u32(const unsigned char *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

static size_t stack_snapshot_size(const shrewd_lv_view_nav_stack_t *stack) {
    size_t total = 4; /* count */
    for (size_t i = 0; i < stack->count; ++i) {
        const shrewd_lv_view_page_desc_t *desc = stack->entries[i].page->desc;
        size_t name_len = (NULL != desc->name) ? strlen(desc->name) : 0;
        total += 12 + name_len + stack->entries[i].params_size;
    }
    return total;
}

static void stack_snapshot_put(unsigned char **out, const shrewd_lv_view_nav_stack_t *stack) {
    put_u32(*out, (uint32_t)stack->count);
    *out += 4;
    for (size_t i = 0; i < stack->count; ++i) {
        const shrewd_lv_view_nav_entry_t *e = &stack->entries[i];
        const char *name = e->page->desc->name;
        uint32_t name_len = (uint32_t)((NULL != name) ? strlen(name) : 0);
        put_u32(*out, e->seq);
        put_u32(*out + 4, name_len);
        put_u32(*out + 8, (uint32_t)e->params_size);
        *out += 12;
        if (0 != name_len) {
            memcpy(*out, name, name_len);
            *out += name_len;
        }
        if (0 != e->params_size) {
            memcpy(*out, e->params, e->params_size);
            *out += e->params_size;
        }
    }
}

int shrewd_lv_view_nav_save_size(const shrewd_lv_view_nav_t *nav, size_t *out_size) {
    if ((NULL == nav) || (NULL == out_size)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    size_t total = 16; /* magic + version + branch_count + current */
    for (size_t i = 0; i < nav->branch_count; ++i) {
        total += stack_snapshot_size(&nav->branches[i]);
    }
    total += stack_snapshot_size(&nav->modal);
    *out_size = total;
    return SHREWD_LV_VIEW_ERR_OK;
}

int shrewd_lv_view_nav_save(const shrewd_lv_view_nav_t *nav, void *buf, size_t size) {
    if ((NULL == nav) || (NULL == buf)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    size_t need = 0;
    if (SHREWD_LV_VIEW_ERR_OK != shrewd_lv_view_nav_save_size(nav, &need)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (size < need) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    unsigned char *out = (unsigned char *)buf;
    put_u32(out, NAV_SNAPSHOT_MAGIC);
    put_u32(out + 4, NAV_SNAPSHOT_VERSION);
    put_u32(out + 8, (uint32_t)nav->branch_count);
    put_u32(out + 12, (uint32_t)nav->current);
    out += 16;
    for (size_t i = 0; i < nav->branch_count; ++i) {
        stack_snapshot_put(&out, &nav->branches[i]);
    }
    stack_snapshot_put(&out, &nav->modal);
    return SHREWD_LV_VIEW_ERR_OK;
}

/** 从快照读回一个栈。 */
static int stack_snapshot_get(shrewd_lv_view_nav_t *nav,
                              shrewd_lv_view_nav_stack_t *stack,
                              lv_obj_t *host,
                              const unsigned char **in,
                              size_t *left) {
    if (4 > *left) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    uint32_t count = get_u32(*in);
    *in += 4;
    *left -= 4;
    if (SHREWD_LV_VIEW_ERR_OK != stack_reserve(stack, (size_t)count)) {
        return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (12 > *left) {
            return SHREWD_LV_VIEW_ERR_ARG_INVALID;
        }
        uint32_t seq = get_u32(*in);
        uint32_t name_len = get_u32(*in + 4);
        uint32_t params_size = get_u32(*in + 8);
        *in += 12;
        *left -= 12;
        if (((size_t)name_len + (size_t)params_size) > *left) {
            return SHREWD_LV_VIEW_ERR_ARG_INVALID;
        }
        char name[128];
        if (name_len >= sizeof(name)) {
            return SHREWD_LV_VIEW_ERR_ARG_INVALID;
        }
        memcpy(name, *in, name_len);
        name[name_len] = '\0';
        *in += name_len;
        *left -= name_len;

        shrewd_lv_view_page_t *page = nav_find_by_name(nav, name);
        if (NULL == page) {
            return SHREWD_LV_VIEW_ERR_NOT_FOUND;
        }
        shrewd_lv_view_nav_entry_t *e = &stack->entries[stack->count];
        memset(e, 0, sizeof(*e));
        e->page = page;
        e->seq = seq;
        e->entered_at = (uint64_t)lv_tick_get();
        if (0 != params_size) {
            void *slot = NULL;
            if (SHREWD_BASIC_ERR_OK != shrewd_basic_platform_alloc(&slot, params_size)) {
                return SHREWD_LV_VIEW_ERR_MEM_NOMEM;
            }
            memcpy(slot, *in, params_size);
            e->params = slot;
            e->params_size = params_size;
        }
        *in += params_size;
        *left -= params_size;
        stack->count += 1;
        if (nav->next_seq <= seq) {
            nav->next_seq = seq + 1;
        }
    }
    return SHREWD_LV_VIEW_ERR_OK;
}

int shrewd_lv_view_nav_restore(shrewd_lv_view_nav_t *nav, const void *buf, size_t size) {
    if ((NULL == nav) || (NULL == buf) || (16 > size)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    const unsigned char *in = (const unsigned char *)buf;
    if (NAV_SNAPSHOT_MAGIC != get_u32(in)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    if (NAV_SNAPSHOT_VERSION != get_u32(in + 4)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    uint32_t branch_count = get_u32(in + 8);
    uint32_t current = get_u32(in + 12);
    in += 16;
    size_t left = size - 16;
    if ((SHREWD_LV_VIEW_NAV_BRANCHES < branch_count) || (branch_count <= current)) {
        return SHREWD_LV_VIEW_ERR_ARG_INVALID;
    }
    for (size_t i = 0; i < nav->branch_count; ++i) {
        stack_clear(&nav->branches[i]);
    }
    stack_clear(&nav->modal);
    nav->branch_count = (size_t)branch_count;
    nav->current = (size_t)current;
    for (uint32_t i = 0; i < branch_count; ++i) {
        int status = stack_snapshot_get(nav, &nav->branches[i], nav->host, &in, &left);
        if (SHREWD_LV_VIEW_ERR_OK != status) {
            return status;
        }
    }
    int status = stack_snapshot_get(nav, &nav->modal, nav->modal_host, &in, &left);
    if (SHREWD_LV_VIEW_ERR_OK != status) {
        return status;
    }
    /* 只激活当前栈的栈顶:其余是描述,回退到它们时才建 widget(§6.5)。 */
    shrewd_lv_view_nav_entry_t *top = stack_top(nav_active(nav));
    return (NULL != top) ? entry_activate(nav_active_host(nav), top) : SHREWD_LV_VIEW_ERR_OK;
}
