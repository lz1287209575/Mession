# MObject Avalonia 编辑器

这是新的桌面编辑器骨架，目标是替代当前的 Web 原型，直接对接现有的 `MObjectEditorService`。

## 当前范围

- UI 技术栈：Avalonia UI
- 当前接入资产：怪物配置
- 当前接入后端：`http://127.0.0.1:18081/`
- 已接接口：
  - `GET /api/status`
  - `GET /api/monster-configs/table`
  - `POST /api/monster-configs/batch-save`
  - `POST /api/monster-config/validate`
  - `POST /api/monster-config/export`
  - `POST /api/monster-configs/delete`

## 当前状态

这版已经把项目骨架、表格主视图、检查器、保存 / 校验 / 导出 / 发布的最小链路接上，同时补了：

- 新增空白行
- 多选批量复制
- 多选批量删除
- 扩展行选择
- 基于选中列的向下填充
- 基于选中列的递增填充
- 基于选中列的清空
- 独立的“批量列”和“粘贴起始列”
- 从系统剪贴板粘贴 TSV / 表格文本
- 粘贴起始行 / 起始列 / 选择范围 / 最近一次粘贴摘要
- `另存为选中`
- `复制到分类`
- 表格列宽本地记忆
- 常用快捷键（`Ctrl+S` / `Ctrl+D` / `Ctrl+Shift+D` / `Delete` / `F5`）
- 单资产类型 tabs 骨架

由于当前开发环境未安装 `dotnet` SDK，所以这次提交没有在本机完成实际编译验证。后续需要先安装 .NET 8 SDK，再执行恢复和运行。

## 运行方式

安装 .NET 8 SDK 后，可直接执行：

```bash
Scripts/run_mobject_editor_avalonia.sh
```

脚本会优先检查本地 `MObjectEditorService`，如果 18081 未启动，会尝试自动拉起它。

## 当前未验证点

由于本机没有 `dotnet` SDK，下面这些 Avalonia 交互还没有完成实际编译和运行验证：

- `DataGrid` 的扩展选择事件与 `SelectedItems`
- 列宽持久化时的 `DataGridLength`
- 剪贴板读取 `TryGetTextAsync`

安装 SDK 后，建议优先验证：

1. `dotnet restore`
2. `dotnet build`
3. 表格多选 / 向下填充 / 粘贴表格
4. `另存为选中` / `复制到分类`
5. 批量复制 / 批量删除
6. 关闭再打开后的列宽恢复

## 计划中的下一步

1. 补区域高亮和真正的框选编辑反馈
2. 加入批量重命名 / 查找替换
3. 补充样式资源与中文主题
4. 抽通用 `MObject` 属性编辑面板
5. 逐步接入技能 / Buff / 掉落等资产类型
