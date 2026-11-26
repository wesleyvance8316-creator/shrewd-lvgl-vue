/**
 * @file lv_conf.h
 * @brief lvgl 配置(shrewd-lv-view 依赖构建用)。
 *
 * 这**不是** lvgl `lv_conf_template.h` 的全量拷贝(2640 行)。`lv_conf_internal.h`
 * 给每个选项都兜了默认值,所以这里**只写我们刻意改动的项**,其余跟随 lvgl 默认。
 * 默认值由 `deps/versions.lock` 钉死在 lvgl 9.6.0,升级 lvgl 是一次刻意动作,
 * 届时应回来复审本文件。
 *
 * 本文件会**随 lvgl 一起安装**(见 `tools/build-deps.sh`),所以所有消费者编译到的
 * 是同一份配置。这是 ABI 的硬要求:`lv_obj_class_t` 的布局依赖 `LV_USE_OBJ_PROPERTY`
 * (见 `docs/design.md` §7.3)。
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* ==========================================================================
 * 组件生态必需(design.md §7.3)
 * ========================================================================== */

/**
 * 自定义 widget class:lv_obj_class_t 的完整定义在 lvgl 私有头
 * `src/core/lv_obj_class_private.h`,公共树只有前向声明。不开这个,
 * 私有头不安装,组件库就造不了新控件。
 */
#define LV_USE_PRIVATE_API 1

/**
 * widget class 的类型化属性(LV_PROPERTY_ID),C 世界对 Vue props 的对应。
 * 这些字段在 `lv_obj_class_t` 里是 `#if` 门控的、**插在结构体中间** ——
 * 开关不同则 sizeof 不同、ABI 不兼容。所以构建与所有消费者必须一致。
 */
#define LV_USE_OBJ_PROPERTY 1

/* 属性名表(反射用:属性 id ↔ 名字)。默认关,需要配置界面 / 脚本绑定时再开。 */
#define LV_USE_OBJ_PROPERTY_NAME 0

/* ==========================================================================
 * 可调项
 * ========================================================================== */

/**
 * 内建分配器的内存池。64 KiB 是 lvgl 默认值,按目标板调整 —— 这是嵌入式上
 * 最常动的一个旋钮,写在这里是为了让它可被发现。
 */
#define LV_MEM_SIZE (64 * 1024U)

#endif /* LV_CONF_H */
