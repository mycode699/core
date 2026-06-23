# 可圈 office V4 总体规划：AI 原生工作区

> **版本**: v4.0-draft  
> **创建日期**: 2026-06-23  
> **状态**: planning  
> **参考**: imux AgentChat / WorkspaceAgentMesh / 三层架构

---

## 1. 一句话愿景

**可圈 office V4 是 AI 原生办公控制面** —— 固定的右侧边栏整合 Chat 和 Agent 能力，能对 Writer/Calc/Impress 文档进行智能修改、调整和优化，同时保持人类审批权和完全可追溯的证据链。

---

## 2. 与 V3 的区别

| 维度 | V3 (已完成) | V4 (规划中) |
|---|---|---|
| **产品形态** | 50+ 运行时文件（合约层） | 固定右侧边栏 + 可折叠面板 |
| **Chat 能力** | 基础 Markdown 渲染 + 流式 UI | imux AgentChat 完整能力（@提及/工具调用/历史搜索/Slash 命令） |
| **Agent 能力** | 异步任务队列 (Cowork) | imux WorkspaceAgentMesh 多智能体协作网 |
| **文档操作** | Select-to-Act 浮窗 + DiffReview | 边栏直接修改 + 批量优化 + 跨文档工作流 |
| **控制面** | 单任务调度 | imux 三层架构 (UI/Control/Runtime) + 生命周期状态机 |
| **演示案例** | 英文为主 | 全部中文案例 + 中文演示脚本 |

---

## 3. 核心架构：三层分离

参考 imux 的架构设计，V4 强制拆分：

```
┌─────────────────────────────────────────────────────────────┐
│ UI Layer (sfx2/VCL/sidebar)                                 │
│ - AIChatPanel (固定右侧边栏)                                  │
│ - AIChatComposer (输入框 + @提及 + Slash 命令)                │
│ - AIChatMarkdownView (渲染 + 代码块 + 表格)                   │
│ - AIChatArtifactList (内容对象列表)                           │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Control Plane (kqoffice/source/ai/control)                  │
│ - WorkspaceAgentMesh (多智能体协作网)                         │
│ - SurfaceLifecycleManager (8 状态生命周期)                     │
│ - ResourceBudgetWatchdog (CPU/RSS/输出预算)                   │
│ - SessionStore (manifest.json + workspaces/ + surfaces/)    │
│ - SafeRestore (损坏隔离启动)                                 │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│ Runtime Layer (kqoffice/source/ai/runtime)                  │
│ - Provider (Ollama/OpenAI 适配)                               │
│ - ApplyPlan (7 种补丁类型)                                    │
│ - ShadowDoc (影子文档隔离)                                    │
│ - DiffReview (差异审核面板)                                   │
│ - ContentRegistry (内容对象注册表)                            │
└─────────────────────────────────────────────────────────────┘
```

### 3.1 为什么必须三层分离

imux 的核心教训：**UI 可以坏，控制面不能坏。控制面可以降级，状态不能丢。**

- **UI Layer 卡死** → Control Plane 仍可 inspect/sample/recover
- **Control Plane 降级** → Session Store 保持完整
- **Session 损坏** → Safe Restore 隔离启动，不崩

---

## 4. 固定右侧边栏设计

### 4.1 边栏布局

```
┌────────────────────────────────────────────────────────────┐
│  Writer/Calc/Impress 主文档区                                │
│                                                             │
│   [文档内容...]                           │ AI 工作区 │       │
│                                          │ ┌─────┐ │       │
│                                          │ │ Chat│ │       │
│                                          │ ├─────┤ │       │
│                                          │ │Agent│ │       │
│                                          │ ├─────┤ │       │
│                                          │ │内容 │ │       │
│                                          │ │审核 │ │       │
│                                          │ └─────┘ │       │
│                                          └─────────┘       │
└────────────────────────────────────────────────────────────┘
```

### 4.2 边栏四合一面板

| 面板 | 功能 | imux 参考 |
|---|---|---|
| **Chat** | 对话、@提及、Slash 命令、流式响应 | AgentChatPanel + MarkdownView |
| **Agent** | 多智能体任务网、进度追踪、资源预算 | WorkspaceAgentMesh + SupervisorAgentAPI |
| **内容** | 已注册内容对象、打开/预览/审核 | ContentRegistry + ContentOpener |
| **审核** | 审核队列、证据检查器、DiffReview | ReviewQueueStore + EvidenceInspector |

### 4.3 边栏交互模式

```
1. 用户选中段落 → 边栏自动弹出「改写/扩写/简写/翻译」快捷按钮
2. 用户输入自然语言 → Chat 解析意图 → 路由到对应 Agent
3. Agent 生成 ApplyPlan → ShadowDoc 预演 → DiffReview 审核 → 用户审批 → 应用到主文档
4. 所有操作记录到 AuditLog + SessionStore
```

---

## 5. imux 核心能力移植清单

### 5.1 AgentChat 模块 (10 个组件)

| 组件 | 功能 | V4 文件名 | 优先级 |
|---|---|---|---|
| AgentChatMentionResolver | @提及解析（@文档/@选择/@连接器） | `AgentChatMentionResolver.hxx/cxx` | P0 |
| AgentChatDiffApplier | 差异应用器（补丁合并） | `AgentChatDiffApplier.hxx/cxx` | P0 |
| AgentChatSelectionCapture | 选区捕获（Writer/Calc/Impress） | `AgentChatSelectionCapture.hxx/cxx` | P0 |
| AgentChatProviderAdapter | Provider 适配器（Ollama/OpenAI） | `AgentChatProviderAdapter.hxx/cxx` | P0 |
| AgentChatDiffExtractor | 差异提取器（LLM 输出→ApplyPlan） | `AgentChatDiffExtractor.hxx/cxx` | P0 |
| AgentChatSlashCommands | Slash 命令（/rewrite /expand /chart） | `AgentChatSlashCommands.hxx/cxx` | P1 |
| AgentChatOpenAIStreamingClient | 流式客户端（SSE/分块） | `AgentChatStreamingClient.hxx/cxx` | P0 |
| AgentChatMarkdownView | Markdown 渲染（代码/表格/列表） | 复用 V3 `AIChatMarkdownRenderer` | P0 |
| AgentChatContextBuilder | 上下文构建器（文档+ 选择 + 历史） | `AgentChatContextBuilder.hxx/cxx` | P0 |
| AgentChatHistorySearch | 历史搜索（FTS5+ 向量） | `AgentChatHistorySearch.hxx/cxx` | P1 |

### 5.2 WorkspaceAgentMesh 模块 (7 个组件)

| 组件 | 功能 | V4 文件名 | 优先级 |
|---|---|---|---|
| WorkspaceAgentMesh | 智能体协作网（注册/发现/路由） | `WorkspaceAgentMesh.hxx/cxx` | P0 |
| WorkspaceMeshTaskQueue | 任务队列（优先级 + 依赖） | `WorkspaceMeshTaskQueue.hxx/cxx` | P0 |
| WorkspaceAgentMeshOrchestrator | 编排器（任务分解/结果合并） | `WorkspaceAgentMeshOrchestrator.hxx/cxx` | P0 |
| WorkspaceAutonomyPipeline | 自主流水线（Plan-Act-Observe） | `WorkspaceAutonomyPipeline.hxx/cxx` | P1 |
| WorkspaceSupervisorAgentAPI | 监督者 API（生命周期/预算/恢复） | `WorkspaceSupervisorAgentAPI.hxx/cxx` | P0 |
| WorkspaceGitAIAssist | Git AI 辅助（提交信息/冲突解决） | `WorkspaceGitAIAssist.hxx/cxx` | P2 |
| WorkspacePullRequestOperations | PR 操作（审阅/批注/合并） | `WorkspacePullRequestOperations.hxx/cxx` | P2 |

### 5.3 生命周期与资源管理 (6 个组件)

| 组件 | 功能 | V4 文件名 | 优先级 |
|---|---|---|---|
| SurfaceLifecycleManager | 8 状态生命周期（created/running/idle/busy/blocked/hung/crashed/reaped） | `SurfaceLifecycleManager.hxx/cxx` | P0 |
| ResourceBudgetWatchdog | 资源预算（CPU/RSS/进程数/输出量） | `ResourceBudgetWatchdog.hxx/cxx` | P0 |
| SessionStore | 会话存储（manifest+workspaces+surfaces+scrollback） | `SessionStore.hxx/cxx` | P0 |
| SafeRestore | 安全恢复（损坏隔离/选择性恢复） | `SafeRestore.hxx/cxx` | P0 |
| ScrollbackLayeredStore | 分层存储（UI 热数据 + 磁盘冷数据） | `ScrollbackLayeredStore.hxx/cxx` | P1 |
| OneClickDoctor | 一键诊断包（进程树/状态树/崩溃日志） | `OneClickDoctor.hxx/cxx` | P1 |

---

## 6. 中文演示案例清单

### 6.1 Writer 文档场景 (10 个)

| 编号 | 场景 | 用户指令 | Agent 动作 | 预期输出 |
|---|---|---|---|---|
| W1 | 段落改写 | "把这段改得更正式一些" | WriterRewriteAgent → ApplyPlan → DiffReview | 正式语气版本，保留原意 |
| W2 | 文档扩写 | "把这一节扩展到 1000 字" | WriterExpandAgent → 生成大纲 → 分段扩写 | 结构清晰的长文档 |
| W3 | 内容总结 | "总结这份合同的关键条款" | ContractAnalyzerAgent → 条款提取 → 风险标注 | 条款清单 + 风险点 |
| W4 | 格式清理 | "清理这份文档的格式" | FormatCleanAgent → 统一样式 → 删除冗余 | 格式统一的文档 |
| W5 | 翻译中译英 | "把这段翻译成英文" | TranslateAgent → 双语对照 → 术语检查 | 准确的专业翻译 |
| W6 | 生成目录 | "生成带页码的目录" | TOCGeneratorAgent → 扫描标题 → 插入目录 | 自动更新目录 |
| W7 | 审阅批注 | "给这份报告加批注" | ReviewAgent → 问题识别 → 批注插入 | 可追踪批注列表 |
| W8 | 参考文献 | "添加参考文献格式" | CitationAgent → 识别引用 → 格式化 | APA/GB 格式参考文献 |
| W9 | 邮件草稿 | "根据这份纪要写邮件" | EmailDraftAgent → 提取要点 → 生成草稿 | 专业邮件模板 |
| W10 | 简历优化 | "优化这份简历的描述" | ResumeAgent → 动词强化 → 量化成果 | 更有冲击力的简历 |

### 6.2 Calc 表格场景 (8 个)

| 编号 | 场景 | 用户指令 | Agent 动作 | 预期输出 |
|---|---|---|---|---|
| C1 | 公式生成 | "计算本季度同比增长" | FormulaAgent → 识别范围 → 生成公式 | 正确的 GROWTH 公式 |
| C2 | 数据清理 | "清理这份数据的格式" | DataCleanAgent → 删除空行 → 统一格式 | 干净的数据表 |
| C3 | 图表建议 | "建议合适的图表" | ChartSuggestAgent → 分析数据类型 → 推荐图表 | 柱状图/折线图建议 |
| C4 | 透视表创建 | "创建销售透视表" | PivotAgent → 识别字段 → 创建透视表 | 按地区/产品汇总 |
| C5 | 异常检测 | "找出异常值" | AnomalyDetectAgent → 统计检验 → 标注异常 | 高亮异常单元格 |
| C6 | 预测分析 | "预测下季度销售额" | ForecastAgent → 时间序列 → 生成预测 | 带置信区间的预测 |
| C7 | 数据验证 | "添加数据验证规则" | ValidationAgent → 识别列类型 → 添加规则 | 下拉列表/范围限制 |
| C8 | VBA 生成 | "生成自动化宏" | MacroAgent → 理解需求 → 生成 VBA 代码 | 可运行的宏代码 |

### 6.3 Impress 演示场景 (7 个)

| 编号 | 场景 | 用户指令 | Agent 动作 | 预期输出 |
|---|---|---|---|---|
| P1 | 大纲生成 | "根据这份文档生成 PPT 大纲" | OutlineAgent → 提取要点 → 生成大纲 | 10 页幻灯片结构 |
| P2 | 配色调整 | "调整配色更专业" | DesignAgent → 分析行业 → 推荐配色 | 商务配色方案 |
| P3 | 布局优化 | "优化这一页的布局" | LayoutAgent → 识别元素 → 重新排版 | 平衡的视觉布局 |
| P4 | 图表美化 | "美化这个图表" | ChartDesignAgent → 应用样式 → 调整标签 | 专业图表样式 |
| P5 | 演讲备注 | "添加演讲备注" | SpeakerNotesAgent → 分析内容 → 生成备注 | 每页演讲要点 |
| P6 | 动画建议 | "建议合适的动画" | AnimationAgent → 识别内容类型 → 推荐动画 | 克制的动画方案 |
| P7 | 导出 PDF | "导出为 PDF 并优化" | ExportAgent → 嵌入字体 → 压缩图片 | 小而清晰的 PDF |

### 6.4 跨文档工作流场景 (5 个)

| 编号 | 场景 | 用户指令 | Agent 动作 | 预期输出 |
|---|---|---|---|---|
| X1 | 合同审阅 | "审阅这份合同并与模板对比" | ContractAgent → 条款提取 → 差异对比 | 风险条款清单 |
| X2 | 报告生成 | "根据 Excel 数据生成 Word 报告" | ReportAgent → 读取数据 → 生成图表 → 插入 Word | 完整分析报告 |
| X3 | 演示准备 | "准备季度汇报演示" | PresentationAgent → 读取 Word → 生成 PPT → 添加备注 | 完整演示稿 |
| X4 | 多文档搜索 | "找出所有提到预算的段落" | SearchAgent → 跨文档检索 → 高亮结果 | 搜索结果列表 |
| X5 | 版本对比 | "对比这两个版本的差异" | DiffAgent → 文档对比 → 生成差异报告 | 修订标记文档 |

---

## 7. 中文演示脚本

### 7.1 场景 W1：段落改写（正式化）

```
【前置条件】
- 打开 Writer 文档
- 选中一段口语化的文字

【用户操作】
1. 右侧边栏自动弹出快捷按钮
2. 点击「改写」按钮
3. 在输入框输入：「把这段改得更正式一些」
4. 点击发送

【Agent 处理】
1. AgentChatSelectionCapture 捕获选区
2. AgentChatContextBuilder 构建上下文（选区 + 文档元数据）
3. WriterRewriteAgent 接收请求
4. Provider 调用 Ollama (qwen3:0.6b)
5. 返回改写后的文本
6. ApplyPlanValidator 验证 ApplyPlan 格式
7. ShadowDoc 预演变更
8. DiffReview 显示差异

【用户审批】
1. 查看 DiffReview 差异
2. 点击「接受」按钮
3. 变更应用到主文档
4. AuditLog 记录操作

【预期结果】
- 原文：「我们觉得这个方案挺好的，可以试试看」
- 改写：「经评估，本方案具备可行性，建议予以实施」
- 保留原文档格式
- 可通过 Ctrl+Z 撤销
```

### 7.2 场景 C1：公式生成（同比增长）

```
【前置条件】
- 打开 Calc 表格
- 选中 Q1-Q4 销售数据列

【用户操作】
1. 右侧边栏点击「公式」标签
2. 输入：「计算本季度同比增长」
3. 点击发送

【Agent 处理】
1. AgentChatSelectionCapture 捕获选区范围
2. FormulaAgent 识别数据类型（数值/时间序列）
3. 生成 GROWTH 公式
4. 在相邻单元格预览结果

【用户审批】
1. 查看公式预览
2. 点击「插入公式」
3. 公式应用到选中单元格

【预期结果】
- 生成公式：=GROWTH(B2:E2, B1:E1, F1)
- 显示同比增长率：15.3%
- 公式可编辑
```

### 7.3 场景 X2：跨文档报告生成

```
【前置条件】
- 打开 Calc 销售数据表
- 新建 Writer 空白文档

【用户操作】
1. 右侧边栏切换到「Agent」标签
2. 选择「报告生成」场景
3. 输入：「根据这份 Excel 数据生成 Word 报告」
4. 点击发送

【Agent 处理】
1. WorkspaceAgentMesh 协调多 Agent
2. DataReaderAgent 读取 Calc 数据
3. ChartGeneratorAgent 生成图表（柱状图/折线图）
4. ReportWriterAgent 生成文字分析
5. DocumentAssemblerAgent 组装 Word 文档
6. 插入图表 + 文字 + 目录

【用户审批】
1. 查看生成的 Word 文档预览
2. 在 DiffReview 中查看差异
3. 点击「接受」
4. 新文档保存为「销售分析报告.odt」

【预期结果】
- 生成 5 页报告：封面 + 目录 + 数据图表 + 文字分析 + 结论
- 图表与原始数据链接
- 格式统一专业
```

---

## 8. 里程碑规划

### M1：边栏基础框架 (4 周)

| 任务 | 文件 | 验证命令 | 状态 |
|---|---|---|---|
| M1.1 固定右侧边栏注册 | `sfx2/source/sidebar/AIChatPanel.cxx` | `bash tests/v4-sidebar-registration-test.sh` | ⬜ |
| M1.2 边栏四合一面板布局 | `sfx2/uiconfig/ui/aichatpanel.ui` | `bash tests/v4-panel-layout-test.sh` | ⬜ |
| M1.3 Chat 输入框 + @提及 | `sfx2/source/sidebar/AIChatComposer.hxx/cxx` | `bash tests/v4-composer-mention-test.sh` | ⬜ |
| M1.4 Slash 命令解析 | `sfx2/source/sidebar/AIChatSlashCommands.hxx/cxx` | `bash tests/v4-slash-commands-test.sh` | ⬜ |
| M1.5 Markdown 渲染增强 | 复用 V3 `AIChatMarkdownRenderer` | `bash tests/v4-markdown-render-test.sh` | ⬜ |

### M2：AgentChat 核心 (6 周)

| 任务 | 文件 | 验证命令 | 状态 |
|---|---|---|---|
| M2.1 @提及解析器 | `kqoffice/source/ai/chat/AgentChatMentionResolver.hxx/cxx` | `bash tests/v4-mention-resolver-test.sh` | ⬜ |
| M2.2 选区捕获 (Writer/Calc/Impress) | `kqoffice/source/ai/chat/AgentChatSelectionCapture.hxx/cxx` | `bash tests/v4-selection-capture-test.sh` | ⬜ |
| M2.3 上下文构建器 | `kqoffice/source/ai/chat/AgentChatContextBuilder.hxx/cxx` | `bash tests/v4-context-builder-test.sh` | ⬜ |
| M2.4 流式客户端 | `kqoffice/source/ai/chat/AgentChatStreamingClient.hxx/cxx` | `bash tests/v4-streaming-client-test.sh` | ⬜ |
| M2.5 差异提取器 | `kqoffice/source/ai/chat/AgentChatDiffExtractor.hxx/cxx` | `bash tests/v4-diff-extractor-test.sh` | ⬜ |
| M2.6 差异应用器 | `kqoffice/source/ai/chat/AgentChatDiffApplier.hxx/cxx` | `bash tests/v4-diff-applier-test.sh` | ⬜ |
| M2.7 历史搜索 (FTS5) | `kqoffice/source/ai/chat/AgentChatHistorySearch.hxx/cxx` | `bash tests/v4-history-search-test.sh` | ⬜ |

### M3：WorkspaceAgentMesh (6 周)

| 任务 | 文件 | 验证命令 | 状态 |
|---|---|---|---|
| M3.1 智能体注册/发现 | `kqoffice/source/ai/mesh/WorkspaceAgentMesh.hxx/cxx` | `bash tests/v4-agent-mesh-register-test.sh` | ⬜ |
| M3.2 任务队列 (优先级 + 依赖) | `kqoffice/source/ai/mesh/WorkspaceMeshTaskQueue.hxx/cxx` | `bash tests/v4-mesh-task-queue-test.sh` | ⬜ |
| M3.3 任务编排器 | `kqoffice/source/ai/mesh/WorkspaceAgentMeshOrchestrator.hxx/cxx` | `bash tests/v4-mesh-orchestrator-test.sh` | ⬜ |
| M3.4 监督者 API | `kqoffice/source/ai/mesh/WorkspaceSupervisorAgentAPI.hxx/cxx` | `bash tests/v4-supervisor-api-test.sh` | ⬜ |
| M3.5 自主流水线 | `kqoffice/source/ai/mesh/WorkspaceAutonomyPipeline.hxx/cxx` | `bash tests/v4-autonomy-pipeline-test.sh` | ⬜ |

### M4：生命周期与资源管理 (4 周)

| 任务 | 文件 | 验证命令 | 状态 |
|---|---|---|---|
| M4.1 8 状态生命周期 | `kqoffice/source/ai/control/SurfaceLifecycleManager.hxx/cxx` | `bash tests/v4-lifecycle-states-test.sh` | ⬜ |
| M4.2 资源预算 Watchdog | `kqoffice/source/ai/control/ResourceBudgetWatchdog.hxx/cxx` | `bash tests/v4-resource-budget-test.sh` | ⬜ |
| M4.3 会话存储 (manifest+workspaces) | `kqoffice/source/ai/control/SessionStore.hxx/cxx` | `bash tests/v4-session-store-test.sh` | ⬜ |
| M4.4 安全恢复 | `kqoffice/source/ai/control/SafeRestore.hxx/cxx` | `bash tests/v4-safe-restore-test.sh` | ⬜ |
| M4.5 分层存储 (热 + 冷) | `kqoffice/source/ai/control/ScrollbackLayeredStore.hxx/cxx` | `bash tests/v4-scrollback-layered-test.sh` | ⬜ |
| M4.6 一键诊断 | `kqoffice/source/ai/control/OneClickDoctor.hxx/cxx` | `bash tests/v4-one-click-doctor-test.sh` | ⬜ |

### M5：中文演示案例 (4 周)

| 任务 | 文件 | 验证命令 | 状态 |
|---|---|---|---|
| M5.1 Writer 10 场景 | `docs/product/v4/demos/writer-demos.md` | `bash tests/v4-writer-demo-sweep.sh` | ⬜ |
| M5.2 Calc 8 场景 | `docs/product/v4/demos/calc-demos.md` | `bash tests/v4-calc-demo-sweep.sh` | ⬜ |
| M5.3 Impress 7 场景 | `docs/product/v4/demos/impress-demos.md` | `bash tests/v4-impress-demo-sweep.sh` | ⬜ |
| M5.4 跨文档 5 场景 | `docs/product/v4/demos/cross-doc-demos.md` | `bash tests/v4-cross-doc-demo-sweep.sh` | ⬜ |
| M5.5 演示视频录制 | `docs/product/v4/demos/videos/` | 手动验证 | ⬜ |

### M6：GA 就绪 (4 周)

| 任务 | 文件 | 验证命令 | 状态 |
|---|---|---|---|
| M6.1 性能基线 | `docs/product/v4/perf-baseline.md` | `bash tests/v4-perf-baseline-test.sh` | ⬜ |
| M6.2 崩溃恢复 | `docs/product/v4/crash-recovery.md` | `bash tests/v4-crash-recovery-test.sh` | ⬜ |
| M6.3 可访问性 | `docs/product/v4/accessibility.md` | `bash tests/v4-accessibility-test.sh` | ⬜ |
| M6.4 本地化 (zh-CN/en-US) | `kqoffice/source/ai/i18n/` | `bash tests/v4-i18n-test.sh` | ⬜ |
| M6.5 发布清单 | `docs/product/v4/release-checklist.md` | `bash tests/v4-release-ga-test.sh` | ⬜ |

---

## 9. 验证矩阵

| 领域 | 必需命令 | 频率 |
|---|---|---|
| V4 边栏注册 | `bash tests/v4-sidebar-registration-test.sh` | 每次提交 |
| V4 AgentChat | `bash tests/v4-agent-chat-sweep.sh` | 每日 |
| V4 AgentMesh | `bash tests/v4-agent-mesh-sweep.sh` | 每日 |
| V4 生命周期 | `bash tests/v4-lifecycle-sweep.sh` | 每日 |
| V4 中文演示 | `bash tests/v4-chinese-demo-sweep.sh` | 每周 |
| V2 向后兼容 | `bash bin/v2-harness-sweep.sh` | 每次提交 |
| V3 合约层 | `bash bin/v3-eval-sweep.sh --self-test` | 每周 |

---

## 10. 风险登记

| 风险 | 影响 | 缓解措施 | 负责人 |
|---|---|---|---|
| R1 UI 框架不匹配 | 边栏与现有 sfx2/VCL 模式冲突 | 优先复用现有 sidebar/DiffReview/ApplyPlan 模式 | 架构组 |
| R2 性能开销 | 固定边栏增加内存/CPU 占用 | 懒加载面板、分层存储、资源预算 | 性能组 |
| R3 状态污染 | 多 Agent 并发导致状态混乱 | Control Plane 集中管理、状态机强制转换 | 架构组 |
| R4 中文演示漂移 | 演示案例与实际功能不一致 | 演示脚本自动化验证、每次功能变更同步更新 | 文档组 |
| R5 向后兼容 | V4 变更破坏 V2/V3 功能 | 完整的 V2/V3 回归测试矩阵 | 测试组 |

---

## 11. 完成模板

每个任务完成后记录：

```markdown
### 任务 id: M1.1
- 文件变更：`sfx2/source/sidebar/AIChatPanel.cxx`, `sfx2/uiconfig/ui/aichatpanel.ui`
- 产品行为：固定右侧边栏注册，可通过 Cmd+Shift+A 打开
- 验证命令：`bash tests/v4-sidebar-registration-test.sh`
- 结果：11/11 检查通过
- 剩余风险：无
- 后续任务：M1.2
```

---

## 12. 附录：imux 参考文件清单

| imux 文件 | 用途 | V4 参考章节 |
|---|---|---|
| `Sources/AgentChat/AgentChatMentionResolver.swift` | @提及解析 | §5.1 |
| `Sources/AgentChat/AgentChatDiffApplier.swift` | 差异应用 | §5.1 |
| `Sources/AgentChat/AgentChatSelectionCapture.swift` | 选区捕获 | §5.1 |
| `Sources/AgentChat/AgentChatContextBuilder.swift` | 上下文构建 | §5.1 |
| `Sources/AgentChat/AgentChatHistorySearch.swift` | 历史搜索 | §5.1 |
| `Sources/WorkspaceAgentMesh.swift` | 智能体协作网 | §5.2 |
| `Sources/WorkspaceMeshTaskQueue.swift` | 任务队列 | §5.2 |
| `Sources/WorkspaceAgentMeshOrchestrator.swift` | 任务编排 | §5.2 |
| `Sources/WorkspaceSupervisorAgentAPI.swift` | 监督者 API | §5.2 |
| `Sources/SessionPersistence.swift` | 会话持久化 | §5.3 |
| `cankao.md` | 三层架构/生命周期/资源预算 | §3 |

---

**下一步行动**：

1. 创建 V4 任务清单：`docs/product/v4/v4-todolist.md`
2. 启动 M1.1 边栏基础框架实现
3. 建立中文演示验证自动化
