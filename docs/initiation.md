# shrewd-lvgl-vue 立项文档

> 状态:**立项(2026-11-23),设计定稿——反应核心已归属
> shrewd-basic 的 reflect 模块**。
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
    ViewModel = 反应核心(shrewd-basic reflect:ref/effect/computed/watch)
                + 绑定层(本仓库)
    View = LVGL(retained widget 树,单向绑定即 lv_xxx_set_* 调用)

跨线程桥:dbus recv(分发线程)→ 快照拷贝 → `lv_async_call`(或
eventfd+lv_timer 批量)→ `shrewd_lv_view_set` → 反应图自动推 UI。桥是唯一
引用 LVGL API 的层,必须留在本仓库;反应核心在 shrewd-basic,零 LVGL 依赖。

## 4. 核心设计

- **反应核心(shrewd-basic `reflect/react.h`)**:`shrewd_basic_reflect_*`
  ——ref(值单元,对象调用方内嵌)、effect(**执行期登记**:运行前设
  current-effect 指针,`get` 读到谁就订阅谁;变更触发重跑,清旧边长
  新边;依赖边平台分配、dispose 释放)、computed/watch(effect 的派生
  形态)。机制详解见 shrewd-basic `docs/reflect.md` §11;
- **绑定(本仓库)**:单向 = effect(widget ← ref);`v-model` = 单向 +
  LVGL 事件写回 ref;**列表** = 容器子项按 key 最小对账(增删 lv 对象,
  不做 diff 算法);
- **组件** = 状态 struct + 创建函数(widget 树 + 绑定)+ 销毁函数
  (解绑/删对象/退订总线)。

**依赖边界**:反应核心来自 shrewd-basic(reflect 模块,零 LVGL 依赖);
绑定层依赖 LVGL + shrewd-basic 的 memory/log/error 约定(家族惯例),
不依赖 dbus(桥层才碰)。绑定层可独立单测。

## 5. 命名规范(家法)

**一切公共标识符以 `shrewd_` 开头**——本项目前缀 `shrewd_lv_view_`
(宏 `SHREWD_LV_VIEW_*`),与 shrewd-basic 的 `shrewd_basic_*` 同规。
讨论中的临时名(cview 等)一律不落代码。头文件命名空间
`include/shrewd-lv-view/`,消费者唯一入口 `view.h`。

## 6. 门面隔离

消费者**只 include `shrewd-lv-view/view.h`**;绑定层是自包含私有实现。
反应核心定址 shrewd-basic(reflect 模块),不存在日后搬迁:view.h 只
暴露绑定与组件面,核心经 shrewd-basic 公共头直接引入。shrewd-basic
现处 0.2.0 预 1.0,API 随本项目真实需求自由演进,调形不是版本事件。

## 7. 里程碑

1. ~~**反应核心 + 单测**~~ **已交付于 shrewd-basic**(reflect/react,
   `shrewd-basic-react-tests` 全绿,2026-11-23);
2. **绑定层**(text/range/v-model)+ 组件生命周期;
3. **列表/条件渲染 + dbus 桥** + 真实页面原型倒推 API 手感。

## 8. 已决事项

**反应核心的归属(2026-11-23 定稿)**:写入 shrewd-basic 的 reflect
模块(`shrewd_basic_reflect_*`)。理由:lvgl-vue 开发更纯粹,没有中间
边界(直接依赖,不养私有副本);shrewd-basic 同日降回 0.2.0(预 1.0,
API 自由演进),未定型 API 不惊动冻结面。语义分野:事件("谁变了",
pub/sub、多进程)归 sb_dbus;状态依赖("谁读过谁自动跟",computed
派生、零接线联动)归反应核心。
