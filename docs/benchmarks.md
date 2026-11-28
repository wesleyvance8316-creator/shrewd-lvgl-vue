# 性能 / 内存基线

```
ctest --test-dir build -R bench --output-on-failure
# 或直接跑:./build/tests/shrewd-lv-view-bench
```

**绝对数随机器变,只看关系** —— 用例的断言也是这么写的("虚拟化比全量省"),
数值只做抄录。下面这组是开发机(x86-64、glibc、`-O2`)上的。

---

## 0. 内存有两块,别混着看

| | 谁在里面 | 怎么量 |
|---|---|---|
| **LVGL 池**(`LV_MEM_SIZE`,默认 64KB) | **widget 树** | `lv_mem_monitor()` |
| **libc 堆** | 框架的 struct(ref / effect / binding / 槽位 / 桥投递) | `mallinfo2().uordblks` |

> 这是基线最有用的一条结论:**widget 不吃 libc 堆**,只看 `mallinfo` 会以为界面免费。

## 1. 静态尺寸(纯 `sizeof`,与机器无关)

| 结构 | 字节 |
|---|---|
| `shrewd_basic_reflect_ref_t` | 24 |
| `shrewd_basic_reflect_effect_t` | 32 |
| `shrewd_lv_view_page_t` | 16 |
| `shrewd_lv_view_page_desc_t` | 64 |
| `shrewd_lv_view_nav_entry_t` | 48 |
| `shrewd_lv_view_nav_stack_t` | 24 |
| `shrewd_lv_view_nav_t` | 424 |
| `shrewd_lv_view_target_t` | 24 |
| `shrewd_lv_view_list_t` | 16 |
| `shrewd_lv_view_list_desc_t` | 56 |
| `shrewd_lv_view_shell_t` | 56 |
| `shrewd_lv_view_bridge_t` | 8 |

**导航栈条目 48 字节** —— 栈深上限 32 也就 1.5KB,栈不是内存问题(§architecture.md 6.6
早就说过"栈爆的真风险不是内存,是迷失")。

## 2. 内存

| | LVGL 池 | libc 堆 | 合计 | 每个 |
|---|---|---|---|---|
| **绑定**(`bind_label_text` ×100) | 34 424 | 19 200 | 53 624 | **536 字节** |
| **列表 100 项(全量)** | 46 192 | 3 200 | 49 392 | **494 字节** |
| **列表 100 项(虚拟化)** | 3 720 | 208 | 3 928 | **39 字节** |
| **桥:1000 条待投递** | 0 | 79 248 | 79 248 | **79 字节** |

**虚拟化 / 全量 = 0.08×** —— 100 项只物化可视窗口那几个,省 12 倍。这就是
`item_height` 存在的理由。

拆开看一个绑定的 536 字节:**~344 是 label widget 本身(LVGL 池)**,**~192 是框架的
绑定记录 + effect + 依赖边(libc 堆)**。所以"绑定贵不贵"取决于 widget,不取决于绑定。

## 3. 时间

| | | |
|---|---|---|
| **列表对账:建 100 项** | 221 µs | 2.2 µs/项(建 200 个 widget + 绑定) |
| **列表对账:100 项倒序** | **0.38 µs** | 重排几乎免费 —— 只 `lv_obj_move_to_index`,不重建 |
| **虚拟化滚动 50 行** | 555 µs | 11 µs/行(边界上 teardown + build) |
| **bridge post ×1000** | 23 µs | 23 ns/条(只入队) |
| **bridge drain 1000 条** | 19 µs | 19 ns/条(一次出队 + `ref_set`) |

**`ref_set` 的传播成本**:

| 绑定数 | |
|---|---|
| 1 | 0.10 µs |
| 10 | 1.16 µs |
| 100 | **41.77 µs** |

**超线性** —— 10 倍绑定换来 36 倍时间。单个 ref 上挂几十个绑定要注意;
数据面拆细(ref 分开)比优化绑定层更管用。

## 4. 已知问题(未决)

**大批绑定 widget 被删掉之后,再新建 widget 会在 `lv_theme_apply → lv_obj_add_style`
里读 NULL 崩掉。**

已排除的:

- **不是 LVGL 池耗尽** —— `LV_MEM_SIZE` 64KB → 512KB 一样崩,崩时池才用 10KB;
- **不是纯 LVGL 的模式** —— 同样的"建 100 个 label、DELETE 钩子里 `free` +
  `effect_dispose`、再建 label"不崩;
- **不是我们堆上的内存错误** —— ASAN 对框架自己的分配零报告。

线索:崩在 `lv_obj_add_style` 读 `NULL + 0x10`,`LV_CHECK_ARG(style != NULL)` 在
发行构建里是空的,所以是**主题给了 NULL style**。而纯 LVGL 不崩,说明是框架的
销毁路径改动了 LVGL 的内部状态。

复现:`tests/bench.c` 里把 `bench_ref_set()` 的 box 加上 `lv_obj_delete(box)` 即可。

**顺带修掉的一个真 bug**:列表绑定的容器删除路径有 use-after-free —— 容器的
DELETE 先释放绑定,子项的 DELETE 钩子还在读它(ASAN 实测)。已修:
`list_binding_delete` 先摘掉子项上的钩子再释放。**这条路径已回归干净。**
