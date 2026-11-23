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
 * Reactive core (shrewd_lv_view_ref_t / _get / _set / _effect /
 * _computed / _watch) lands here after the placement decision
 * (docs/initiation.md "open question"); the binding layer and the
 * bus-to-UI bridge follow.
 */

#endif /* SHREWD_LV_VIEW_VIEW_H */
