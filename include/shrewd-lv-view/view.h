#ifndef SHREWD_LV_VIEW_VIEW_H
#define SHREWD_LV_VIEW_VIEW_H

/**
 * @file view.h
 * @brief The facade of shrewd-lv-view: the ONLY header consumers
 *        include. Vue-style reactive primitives, widget bindings and
 *        the bus-to-UI bridge all surface through this header; the
 *        implementation behind it is a private detail and may move
 *        between repositories without touching consumer code
 *        (docs/initiation.md).
 *
 * Naming law: every public identifier is shrewd_lv_view_* (macros
 * SHREWD_LV_VIEW_*), like shrewd-basic's shrewd_basic_*.
 */

/** Library version string (SemVer; 0.x = API free to change). */
#define SHREWD_LV_VIEW_VERSION_STRING "0.1.0"

/** Return the library version string. */
const char *shrewd_lv_view_version(void);

/*
 * The binding layer lands here (docs/initiation.md milestone 2): text /
 * range / v-model bindings and the component lifecycle. The reactive
 * core is not duplicated in this facade - it lives in shrewd-basic
 * (shrewd_basic_reflect_ref_t / _effect / _computed / _watch) and its
 * public header travels with this package.
 */

#endif /* SHREWD_LV_VIEW_VIEW_H */
