# shrewd-lvgl-vue 立项文档

> 状态:**立项(2026-11-23),设计定稿待反应核心归属决断**。
> 面向嵌入式 Linux 的 LVGL 复杂业务,以 Vue3 的设计思路与架构,
> 提供**纯 C、零中间过程**的开发模型。

## 1. 背景与需求

LVGL 越来越多地进入嵌入式 Linux 且业务复杂化;手写 LVGL 的痛点是
状态变更处散落手工 UI 同步(漏一处即 bug)、逻辑与视图纠缠、复用
困难。Web 前端的 Vue3 已验证过"反应式数据流 + 组件化"的解法。

**不是什么**:不是把 Vue3/TS 编译成 C;使用者是纯 C 开发者,无模板
编译、无代码生成、无中间产物——API 即产品。

## 2. 目标与非目标

**目标**:状态 → UI 自动传播;组件化(状态 struct + 创建/销毁函数);
v-model 级双向绑定;列表/条件渲染;与 sb_dbus 数据总线无缝合流
(跨模块/跨进程状态直达 UI)。

**非目标(不抄的泥潭)**:vdom/diff(LVGL 本是 retained widget 树)、
模板编译、Pinia/VueX/Router/SSR、隐藏线程。

## 3. 架构:MVVM 合流

    Model = sb_dbus(shm/inproc,类型化命名空间 + 钩子 + 通知管道)
    ViewModel = 反应核心(ref/effect/computed/watch)+ 绑定层
    View = LVGL(retained widget 树,单向绑定即 lv_xxx_set_* 调用)

跨线程桥:dbus recv(分发线程)→ 快照拷贝 → `lv_async_call`(或
eventfd+lv_timer 批量)→ `shrewd_lv_view_set` → 反应图自动推 UI。桥是唯一
引用 LVGL API 的层,必须留在本仓库;反应核心零 LVGL 依赖。

## 4. 核心设计

- **shrewd_lv_view_ref_t**:值 + 订阅表(侵入式双链,零堆分配;用户结构体内嵌);
- **shrewd_lv_view_effect**:**执行期登记**——运行前设 current-effect 指针,`get`
  读到谁就订阅谁;变更触发重跑(清旧边长新边);
- **computed/watch**:effect 的派生形态;
- **绑定**:单向 = effect(widget ← ref);`v-model` = 单向 + LVGL
  事件写回 ref;**列表** = 容器子项按 key 最小对账(增删 lv 对象,
  不做 diff 算法);
- **组件** = 状态 struct + 创建函数(widget 树 + 绑定)+ 销毁函数
  (解绑/删对象/退订总线)。

**依赖边界**:仅 shrewd-basic 的 memory/log/error 约定(家族惯例);
core 不依赖 LVGL、不依赖 ds(侵入式链自建 ~40 行)、不依赖 dbus
(桥层才碰)。核心可独立单测。

## 5. 命名规范(家法)

**一切公共标识符以 `shrewd_` 开头**——本项目前缀 `shrewd_lv_view_`
(宏 `SHREWD_LV_VIEW_*`),与 shrewd-basic 的 `shrewd_basic_*` 同规。
讨论中的临时名(cview 等)一律不落代码。头文件命名空间
`include/shrewd-lv-view/`,消费者唯一入口 `view.h`。

## 6. 门面隔离与升格路径

消费者**只 include `shrewd-lv-view/view.h`**;core 是自包含私有实现。若
日后反应核心升格进 shrewd-basic(第二个消费者出现):core 文件
原样搬迁 → basic 侧命名 `shrewd_basic_*` → 本仓库 `view.h` 变薄
别名层——**消费方源码零改动**。反之若直接放 basic,1.0 承诺面将
被未定型 API 绑住,每次调形都是一次版本事件。

## 7. 里程碑

1. **反应核心 + 单测**(零 LVGL 依赖,纯断言可跑);
2. **绑定层**(text/range/v-model)+ 组件生命周期;
3. **列表/条件渲染 + dbus 桥** + 真实页面原型倒推 API 手感。

## 8. 开放问题(讨论中)

**反应核心的归属**:lvgl-vue(推荐:API 手感随产品迭代、不惊动
basic 1.0 承诺面、单消费者合规)vs 提前进 shrewd-basic(免未来
迁移,但违家法、未定型 API 进冻结面)。判据一句话:进了 basic 的
东西必须"项目死了还能用"。倾向与结论待定,定稿后更新本节。
