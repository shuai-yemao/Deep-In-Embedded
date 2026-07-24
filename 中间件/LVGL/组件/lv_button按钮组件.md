---
tags: [LVGL, button, GUI_Guider]
date: 2026-07-24
---

# 📖 引言

> 这篇笔记说明 `lv_button` 的角色、基础用法，以及它和通用 `lv_obj_*` 接口的关系。

---

# 📝 lv_button按钮组件

> `lv_button` 是建立在 `lv_obj` 基础上的可点击组件，适合用作界面中的操作入口。

## 实际意义

- 为界面提供统一的点击入口。
- 常与 `label`、`image` 等子对象组合，形成文字按钮或图标按钮。
- 在 GUI Guider 中经常被作为最基础的交互组件。

## 应用场景

- 确认/取消/进入下一级页面
- 开关式按钮、工具按钮
- 列表项中的交互入口

## 核心逻辑/原理

1. `lv_button` 的特有创建入口主要是 `lv_button_create(parent)`。
2. 按钮的位置、尺寸、样式、事件大多通过通用 `lv_obj_*` 接口完成。
3. `lv_button` 绑定的是 `lv_button_class`，因此默认行为、状态语义和交互定位都不同于普通 `lv_obj`。

```c
lv_obj_t * btn = lv_button_create(parent);
lv_obj_set_pos(btn, 20, 20);
lv_obj_set_size(btn, 100, 40);

lv_obj_t * label = lv_label_create(btn);
lv_label_set_text(label, "OK");

lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, NULL);
```

## 关键公式/结论

1. `lv_button` 的核心创建函数是 `lv_button_create`。
2. 大部分按钮控制依赖 `lv_obj_set_pos`、`lv_obj_set_size`、`lv_obj_add_style`、`lv_obj_add_event_cb` 等通用接口。
3. 按钮重点状态是 `LV_STATE_DISABLED` 和 `LV_STATE_CHECKED`，常配合 `LV_OBJ_FLAG_CHECKABLE` 使用。

## 实际操作步骤

### 第一步

创建按钮对象：`lv_button_create(parent)`。

### 第二步

使用 `lv_obj_set_pos`、`lv_obj_set_size` 调整按钮位置和尺寸。

### 第三步

在按钮内部创建 `lv_label`，设置文字，再绑定点击事件。

## 常见问题

> 现象 → 根因 → 修复。

### 发现的问题

把按钮文字当成 button 自己的一个 part 来处理。

### 根因分析

`lv_button` 本体通常只有 `LV_PART_MAIN`，而文字通常来自内部单独创建的 `lv_label` 子对象，不是 button 的另一个 part。

### 改进方法

按钮本体样式改 `MAIN`，文字颜色、字体、字间距优先改内部 label 的文本样式。

---

# 💬 Q&A

## 🟢 基础

### Q1

写一个最小可用 button，最少会用到哪些函数？

A1：通常至少会用到 `lv_button_create`、`lv_obj_set_pos`、`lv_obj_set_size`、`lv_label_create`、`lv_label_set_text` 和 `lv_obj_add_event_cb`。

### Q2

`lv_button` 和普通 `lv_obj` 最本质的区别是什么？

A2：最根本区别是它们绑定的 class 不同，`lv_button` 绑定的是 `lv_button_class`，因此具有更明确的点击交互语义和默认行为。

## 🟡 进阶

### Q3

什么是 `part`，什么是 `child object`？

A3：`part` 是同一个对象内部在样式系统中可区分的一部分；`child object` 是挂在当前对象下面的另一个独立对象。

## 🔴 困难

### Q4

如何把 button 做成开关式按钮？

A4：给按钮设置 `LV_OBJ_FLAG_CHECKABLE`，点击后会在 `LV_STATE_CHECKED` 和非选中之间切换，并触发 `LV_EVENT_VALUE_CHANGED`，应用层再根据状态执行逻辑。

---

# 📋 总结

`lv_button` 的特有函数其实不多，核心是 `lv_button_create`，其余大多数控制依赖通用 `lv_obj_*` 接口。理解 button 的关键，不是把它当成普通容器加样式，而是看见它背后的 class、state、flag 和 event 语义。按钮本体负责交互，内部 `label` 子对象负责文字，这条边界要分清。后续再学 `label`、`list`、`switch` 时，这套思路都还能复用。

---

# 📎 参考资料

## 🎥 视频链接

- 未记录

## 🔗 博客/文档链接

- [Button (lv_button)](https://docs.lvgl.io/master/details/widgets/button.html) — 官方按钮文档。

## 💻 仓库链接

- [lvgl/lvgl](https://github.com/lvgl/lvgl)

## 📄 代码/附件

- [[LVGL开发指南-ESP32S3 LVGL移植教程.pdf]]
- [[LVGL开发指南_V1.5.pdf]]
