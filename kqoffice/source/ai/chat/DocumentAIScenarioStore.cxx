/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include <DocumentAIScenarioStore.hxx>

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace kqoffice::ai::chat
{
namespace
{
OUString envOrEmpty(const char* name)
{
    const char* p = std::getenv(name);
    if (!p || !*p)
        return OUString();
    return OUString::fromUtf8(p);
}

OUString readFileUtf8(const OUString& rSystemPath)
{
    std::ifstream in(OUStringToOString(rSystemPath, RTL_TEXTENCODING_UTF8).getStr());
    if (!in)
        return OUString();
    std::ostringstream ss;
    ss << in.rdbuf();
    return OUString::fromUtf8(ss.str().c_str());
}

bool writeFileUtf8(const OUString& rSystemPath, const std::string& body)
{
    std::ofstream out(OUStringToOString(rSystemPath, RTL_TEXTENCODING_UTF8).getStr(),
                      std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << body;
    return static_cast<bool>(out);
}

void appendJsonEscaped(OUStringBuffer& b, const OUString& s)
{
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        if (c == u'\\' || c == u'"')
            b.append(u'\\');
        if (c == u'\n')
        {
            b.append(u"\\n");
            continue;
        }
        if (c == u'\r')
        {
            b.append(u"\\r");
            continue;
        }
        if (c == u'\t')
        {
            b.append(u"\\t");
            continue;
        }
        b.append(c);
    }
}

void appendJsonString(OUStringBuffer& b, std::u16string_view key, const OUString& value)
{
    b.append(u"    \"");
    b.append(key);
    b.append(u"\": \"");
    appendJsonEscaped(b, value);
    b.append(u"\"");
}

void appendJsonBool(OUStringBuffer& b, std::u16string_view key, bool v)
{
    b.append(u"    \"");
    b.append(key);
    b.append(u"\": ");
    b.append(v ? u"true"_ustr : u"false"_ustr);
}

void appendJsonInt(OUStringBuffer& b, std::u16string_view key, sal_Int32 v)
{
    b.append(u"    \"");
    b.append(key);
    b.append(u"\": ");
    b.append(v);
}

OUString jsonStringField(const OUString& rJson, std::u16string_view key, sal_Int32 from = 0)
{
    OUString needle = u"\""_ustr + OUString(key) + u"\""_ustr;
    sal_Int32 pos = rJson.indexOf(needle, from);
    if (pos < 0)
        return OUString();
    pos = rJson.indexOf(u':', pos + needle.getLength());
    if (pos < 0)
        return OUString();
    while (pos + 1 < rJson.getLength()
           && (rJson[pos + 1] == u' ' || rJson[pos + 1] == u'\t' || rJson[pos + 1] == u'\n'
               || rJson[pos + 1] == u'\r'))
        ++pos;
    if (pos + 1 >= rJson.getLength() || rJson[pos + 1] != u'"')
        return OUString();
    sal_Int32 start = pos + 2;
    OUStringBuffer buf;
    for (sal_Int32 i = start; i < rJson.getLength(); ++i)
    {
        const sal_Unicode c = rJson[i];
        if (c == u'\\')
        {
            if (i + 1 >= rJson.getLength())
                break;
            const sal_Unicode n = rJson[++i];
            if (n == u'n')
                buf.append(u'\n');
            else if (n == u't')
                buf.append(u'\t');
            else if (n == u'r')
                buf.append(u'\r');
            else
                buf.append(n);
            continue;
        }
        if (c == u'"')
            break;
        buf.append(c);
    }
    return buf.makeStringAndClear();
}

bool jsonBoolField(const OUString& rJson, std::u16string_view key, bool def, sal_Int32 from = 0)
{
    OUString needle = u"\""_ustr + OUString(key) + u"\""_ustr;
    sal_Int32 pos = rJson.indexOf(needle, from);
    if (pos < 0)
        return def;
    pos = rJson.indexOf(u':', pos + needle.getLength());
    if (pos < 0)
        return def;
    while (pos + 1 < rJson.getLength()
           && (rJson[pos + 1] == u' ' || rJson[pos + 1] == u'\t' || rJson[pos + 1] == u'\n'
               || rJson[pos + 1] == u'\r'))
        ++pos;
    if (pos + 1 >= rJson.getLength())
        return def;
    if (rJson.match(u"true"_ustr, pos + 1))
        return true;
    if (rJson.match(u"false"_ustr, pos + 1))
        return false;
    return def;
}

sal_Int32 jsonIntField(const OUString& rJson, std::u16string_view key, sal_Int32 def,
                       sal_Int32 from = 0)
{
    OUString needle = u"\""_ustr + OUString(key) + u"\""_ustr;
    sal_Int32 pos = rJson.indexOf(needle, from);
    if (pos < 0)
        return def;
    pos = rJson.indexOf(u':', pos + needle.getLength());
    if (pos < 0)
        return def;
    while (pos + 1 < rJson.getLength()
           && (rJson[pos + 1] == u' ' || rJson[pos + 1] == u'\t' || rJson[pos + 1] == u'\n'
               || rJson[pos + 1] == u'\r'))
        ++pos;
    sal_Int32 start = pos + 1;
    sal_Int32 end = start;
    if (end < rJson.getLength() && rJson[end] == u'-')
        ++end;
    while (end < rJson.getLength() && rJson[end] >= u'0' && rJson[end] <= u'9')
        ++end;
    if (end <= start)
        return def;
    return rJson.copy(start, end - start).toInt32();
}

DocumentAIScenario makeBuiltin(const OUString& id, const OUString& title, const OUString& cat,
                               const OUString& surface, const OUString& cap, const OUString& slash,
                               const OUString& tmpl, sal_Int32 order, bool agent = false)
{
    DocumentAIScenario s;
    s.id = id;
    s.titleZh = title;
    s.category = cat;
    s.preferredSurface = surface;
    s.capabilityHint = cap;
    s.slashCommand = slash;
    s.promptTemplate = tmpl;
    s.sortOrder = order;
    s.builtin = true;
    s.enabled = true;
    s.options.showAsButton = true;
    s.options.attachSelection = true;
    s.options.includeDocContext = true;
    s.options.autoSubmit = true;
    s.options.requireApproval = true;
    s.options.useAgentPipeline = agent;
    return s;
}
} // namespace

OUString DocumentAIScenarioStore::defaultConfigPath()
{
    OUString overridePath = envOrEmpty("KQOFFICE_AI_SCENARIOS");
    if (!overridePath.isEmpty())
        return overridePath;
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return OUString();
    return OUString::fromUtf8(home) + u"/.config/kqoffice/ai-scenarios.json"_ustr;
}

std::vector<DocumentAIScenario> DocumentAIScenarioStore::builtinDefaults()
{
    std::vector<DocumentAIScenario> v;
    // —— Writer ——
    v.push_back(makeBuiltin(
        u"official-polish"_ustr, u"公文润色"_ustr, u"writer"_ustr, u"writer"_ustr, u"rewrite"_ustr,
        u"/公文润色"_ustr,
        u"【公文润色】庄重准确、删繁就简。保留关键数据。输出改写全文或 ApplyPlan。\n原文：\n{selection}"_ustr,
        10));
    v.back().options.pinned = true; // default 常用
    // —— 公文包（垂类）——
    v.push_back(makeBuiltin(
        u"official-notice"_ustr, u"通知公告"_ustr, u"writer"_ustr, u"writer"_ustr, u"plan"_ustr,
        u"/通知"_ustr,
        u"【通知公告】按机关公文习惯起草。结构：标题 / 主送 / 正文（事由-事项-要求）/ "
        u"落款日期。语气庄重；缺信息用【待填】。\n素材：\n{selection}"_ustr,
        12));
    v.push_back(makeBuiltin(
        u"official-request"_ustr, u"请示函"_ustr, u"writer"_ustr, u"writer"_ustr, u"plan"_ustr,
        u"/请示"_ustr,
        u"【请示/函】写清：缘由、依据、具体请求、办结时限。一文一事。\n素材：\n{selection}"_ustr,
        13));
    v.push_back(makeBuiltin(
        u"official-summary"_ustr, u"工作总结"_ustr, u"writer"_ustr, u"writer"_ustr, u"plan"_ustr,
        u"/工作总结"_ustr,
        u"【工作总结】结构：总体概述 → 主要成绩（条列+数据）→ 问题不足 → 下阶段计划。\n"
        u"素材：\n{selection}"_ustr,
        14));
    v.push_back(makeBuiltin(
        u"official-pack"_ustr, u"公文包"_ustr, u"writer"_ustr, u"writer"_ustr, u"review"_ustr,
        u"/公文包"_ustr,
        u"【公文包 · 清单】针对下列材料输出：\n"
        u"1) 文种判定（通知/请示/报告/纪要/函）；\n"
        u"2) 必备要素检查清单（标题、主送、依据、事项、时限、落款）；\n"
        u"3) 风险表述与修改建议；\n"
        u"4) 一版可直接使用的改写稿。\n"
        u"材料：\n{selection}"_ustr,
        11));
    v.back().options.pinned = true;
    v.push_back(makeBuiltin(
        u"rewrite-smooth"_ustr, u"通顺改写"_ustr, u"writer"_ustr, u"writer"_ustr, u"rewrite"_ustr,
        u"/通顺改写"_ustr,
        u"【通顺改写】提升可读性，不改变事实与语气强度。\n原文：\n{selection}"_ustr, 20));
    v.push_back(makeBuiltin(
        u"shorten"_ustr, u"精简压缩"_ustr, u"writer"_ustr, u"writer"_ustr, u"summarize"_ustr,
        u"/精简"_ustr,
        u"【精简】保留核心信息，压缩到约一半篇幅。\n原文：\n{selection}"_ustr, 30));
    v.push_back(makeBuiltin(
        u"expand"_ustr, u"扩写丰富"_ustr, u"writer"_ustr, u"writer"_ustr, u"rewrite"_ustr,
        u"/扩写"_ustr,
        u"【扩写】在不编造事实前提下补充细节与过渡。\n原文：\n{selection}"_ustr, 40));
    v.push_back(makeBuiltin(
        u"translate-en"_ustr, u"译为英文"_ustr, u"writer"_ustr, u"writer"_ustr, u"rewrite"_ustr,
        u"/译英"_ustr,
        u"【翻译】将下列中文译为专业英文，保持格式。\n原文：\n{selection}"_ustr, 50));
    v.push_back(makeBuiltin(
        u"translate-zh"_ustr, u"译为中文"_ustr, u"writer"_ustr, u"writer"_ustr, u"rewrite"_ustr,
        u"/译中"_ustr,
        u"【翻译】将下列内容译为简洁中文。\n原文：\n{selection}"_ustr, 60));
    v.push_back(makeBuiltin(
        u"proofread"_ustr, u"校对审阅"_ustr, u"writer"_ustr, u"writer"_ustr, u"review"_ustr,
        u"/校对"_ustr,
        u"【校对】标出错别字、标点、逻辑问题，并给出修改稿。\n原文：\n{selection}"_ustr, 70));
    v.push_back(makeBuiltin(
        u"minutes"_ustr, u"会议纪要"_ustr, u"writer"_ustr, u"writer"_ustr, u"summarize"_ustr,
        u"/会议纪要"_ustr,
        u"【会议纪要】整理为：议题 / 结论 / 负责人 / 截止时间。\n素材：\n{selection}"_ustr, 80));
    v.push_back(makeBuiltin(
        u"report-structure"_ustr, u"汇报结构"_ustr, u"writer"_ustr, u"writer"_ustr, u"plan"_ustr,
        u"/汇报结构"_ustr,
        u"【汇报结构】输出：背景-进展-问题-计划-所需支持。\n素材：\n{selection}"_ustr, 90));
    // 空白页 AI 起草（开始中心 / 侧栏一键）
    v.push_back(makeBuiltin(
        u"blank-draft-writer"_ustr, u"AI 起草文档"_ustr, u"writer"_ustr, u"writer"_ustr,
        u"plan"_ustr, u"/AI起草"_ustr,
        u"【空白文档 AI 起草】根据主题写出可直接落地的完整初稿（标题+分段正文）。\n"
        u"要求：\n"
        u"1) 结构清晰（标题、导语、分节、小结/下一步）；\n"
        u"2) 语气专业克制，不编造无法核实的数据；缺信息用【待填】标注；\n"
        u"3) 只输出可粘贴进正文的成稿，不要元解释。\n"
        u"主题/素材（可为空，则按通用工作汇报骨架起草）：\n{selection}"_ustr,
        5));
    v.back().options.pinned = true;
    v.back().options.autoSubmit = false; // 填入提示后等用户补充主题再发送
    v.back().options.attachSelection = true;

    // —— Calc ——
    v.push_back(makeBuiltin(
        u"formula-assist"_ustr, u"公式助手"_ustr, u"calc"_ustr, u"calc"_ustr, u"chat"_ustr,
        u"/公式助手"_ustr,
        u"【公式助手 / 就地公式栏感】为当前单元格或选区给出可写入的公式。要求：\n"
        u"1) 第一行必须是以 = 开头的完整公式（单独一行，便于 Ctrl+K / 侧栏写回单元格）；\n"
        u"2) 再简要说明用途与引用区域；\n"
        u"3) 可选输出 ApplyPlan JSON：\n"
        u"```json\n{\"plan_id\":\"ap-formula\",\"operations\":[{\"op_type\":\"replace\","
        u"\"target\":\"cell:A1\",\"new_text\":\"=SUM(A1:A10)\"}]}\n```\n"
        u"选区：\n{selection}"_ustr,
        110));
    v.back().options.pinned = true;
    v.push_back(makeBuiltin(
        u"blank-draft-calc"_ustr, u"AI 建表"_ustr, u"calc"_ustr, u"calc"_ustr, u"plan"_ustr,
        u"/AI建表"_ustr,
        u"【空白表格 AI 建表】根据主题设计表头与示例行，并给出关键汇总公式。\n"
        u"要求：\n"
        u"1) 先给出表头行（用制表符或逗号分隔列名）；\n"
        u"2) 2–3 行示例数据；\n"
        u"3) 再单独列出以 = 开头的汇总/比率公式（每行一个）；\n"
        u"4) 简短说明如何使用。\n"
        u"主题/素材：\n{selection}"_ustr,
        105));
    v.back().options.autoSubmit = false;
    v.push_back(makeBuiltin(
        u"data-clean"_ustr, u"数据清洗"_ustr, u"calc"_ustr, u"calc"_ustr, u"extract"_ustr,
        u"/数据清洗"_ustr,
        u"【数据清洗】指出空值/重复/格式问题，给出处理步骤与示例公式。\n选区：\n{selection}"_ustr,
        120));
    v.push_back(makeBuiltin(
        u"data-summary"_ustr, u"数据汇总"_ustr, u"calc"_ustr, u"calc"_ustr, u"summarize"_ustr,
        u"/数据汇总"_ustr,
        u"【数据汇总】描述分布、极值、异常，建议透视/汇总公式。\n选区：\n{selection}"_ustr,
        130));
    v.push_back(makeBuiltin(
        u"explain-cells"_ustr, u"解释单元格"_ustr, u"calc"_ustr, u"calc"_ustr, u"chat"_ustr,
        u"/解释单元格"_ustr,
        u"【解释】用通俗语言解释选中内容含义与可能用途。\n选区：\n{selection}"_ustr, 140));

    // —— Impress · Wave UI-4 设计流（墨刀/Claude Design 心智）——
    // 禁止「一句话黑盒成片」：先大纲 → 多方案卡片 → 用户选一 → 批准写回 → 导出 PPTX。
    // ① 大纲：chat only，不产出可写回 ## 页结构，避免误触 apply。
    v.push_back(makeBuiltin(
        u"design-outline"_ustr, u"① 先写大纲"_ustr, u"impress"_ustr, u"impress"_ustr,
        u"chat"_ustr, u"/先写大纲"_ustr,
        u"【演示设计流 · 步骤① 仅大纲 · 禁止直接写回幻灯】\n"
        u"根据主题/素材只输出「可评审大纲」，不要使用 ## 1. 幻灯写回格式。\n"
        u"输出结构：\n"
        u"标题：（演示名）\n"
        u"页数建议：N\n"
        u"1. 章节/页标题 — 一句话目的\n"
        u"   - 要点…\n"
        u"2. …\n"
        u"讲者节奏：（总时长建议）\n"
        u"缺信息用【待填】。结束后提示用户：点「② 多方案」对比 2–3 版结构。\n"
        u"主题/素材：\n{selection}"_ustr,
        200));
    v.back().options.pinned = true;
    v.back().options.autoSubmit = false;
    v.back().options.requireApproval = true;

    // ② 多方案：并列 方案A/B/C，仍不写回。
    v.push_back(makeBuiltin(
        u"design-variants"_ustr, u"② 多方案"_ustr, u"impress"_ustr, u"impress"_ustr,
        u"chat"_ustr, u"/多方案"_ustr,
        u"【演示设计流 · 步骤② 多方案板 · 对标墨刀/Claude Design】\n"
        u"基于主题与（若有）上文大纲，给出 **恰好 3 套** 可并列比较的结构方案。\n"
        u"每套格式：\n"
        u"### 方案A · 名称（一句话定位）\n"
        u"- 适合：…\n"
        u"- 页数：…\n"
        u"- 页序：1… 2… 3…（只列标题，不写 ## 写回体）\n"
        u"- 风格：商务克制 / 故事线 / 数据汇报 等其一\n"
        u"同样给出 方案B、方案C。\n"
        u"最后用一行：请回复「选方案A/B/C」或点「③ 选一写回」并写明选用方案。\n"
        u"**禁止**在本步输出可自动写回的 ## 1. 幻灯体。\n"
        u"主题/素材：\n{selection}"_ustr,
        201));
    v.back().options.pinned = true;
    v.back().options.autoSubmit = false;

    // ③ 选一写回：仅在用户已选定方案后，产出 ## 写回体；须批准。
    v.push_back(makeBuiltin(
        u"design-apply"_ustr, u"③ 选一写回"_ustr, u"impress"_ustr, u"impress"_ustr,
        u"plan"_ustr, u"/选一写回"_ustr,
        u"【演示设计流 · 步骤③ 选定方案 → 可写回幻灯 · 须用户批准】\n"
        u"前提：用户已从多方案中选定一套（若未指定，默认采用上文「方案A」）。\n"
        u"把选定方案展开为可写回多页结构。每页必须用：\n"
        u"## 1. 标题\n"
        u"版式：标题页|标题内容|分栏|章节\n"
        u"主题：商务蓝\n"
        u"- 要点1\n- 要点2\n- 要点3\n"
        u"讲稿：30–60 秒（可选）\n"
        u"配图：画面描述（可选，写回为占位）\n"
        u"页数 5–10；首页标题页；结尾总结/下一步。缺信息【待填】。\n"
        u"说明：写回后主文档仍须用户点「批准写回」；未批准零变更。\n"
        u"主题/已选方案/素材：\n{selection}"_ustr,
        202));
    v.back().options.pinned = true;
    v.back().options.autoSubmit = false;
    v.back().options.requireApproval = true;

    // ④ 导出：操作指引（本地文件→导出），不调云、不黑盒。
    v.push_back(makeBuiltin(
        u"design-export"_ustr, u"④ 导出 PPTX"_ustr, u"impress"_ustr, u"impress"_ustr,
        u"chat"_ustr, u"/导出PPTX"_ustr,
        u"【演示设计流 · 步骤④ 导出】\n"
        u"用简洁中文说明如何把当前演示导出为 PPTX（不执行导出、不改文档）：\n"
        u"1) 菜单：文件 → 导出为 → 导出为 PPTX…（或另存为 .pptx）\n"
        u"2) 选择路径与文件名后保存\n"
        u"3) 若需兼容投影：检查字体是否嵌入/替换\n"
        u"4) 提醒：AI 只协助内容；导出始终由用户确认\n"
        u"若上文有页数，可附「建议检查清单」3 条。\n"
        u"补充：\n{selection}"_ustr,
        203));
    v.back().options.autoSubmit = true; // 纯指引，可直接生成说明
    v.back().options.requireApproval = true;

    // 兼容旧 id：outline-to-slides = ③ 写回体（须批准，禁止 auto 黑盒）
    v.push_back(makeBuiltin(
        u"outline-to-slides"_ustr, u"③ 选一写回"_ustr, u"impress"_ustr, u"impress"_ustr,
        u"plan"_ustr, u"/大纲成片"_ustr,
        u"【兼容 · 等同设计流③】仅在用户已有明确大纲/选定方案时使用。\n"
        u"输出可写回多页（## N. 标题 + 版式 + 要点 + 可选讲稿/配图）。\n"
        u"**禁止**在用户未提供主题/大纲时凭空编造整本演示。\n"
        u"写回须用户批准。素材：\n{selection}"_ustr,
        210));
    v.back().options.autoSubmit = false;
    v.back().options.requireApproval = true;
    v.back().options.showAsButton = false; // 避免与 design-apply 双按钮

    // 旧「AI 成片」：降级为「从主题进入设计流」，禁止一键黑盒写回
    v.push_back(makeBuiltin(
        u"blank-draft-impress"_ustr, u"从主题开始"_ustr, u"impress"_ustr, u"impress"_ustr,
        u"chat"_ustr, u"/从主题开始"_ustr,
        u"【禁止黑盒成片】不要一次生成可写回 ## 幻灯体。\n"
        u"请只做设计流①：输出页序大纲（标题+目的+页数建议），然后请用户点「② 多方案」。\n"
        u"主题/素材：\n{selection}"_ustr,
        205));
    v.back().options.autoSubmit = false;
    v.back().options.requireApproval = true;

    v.push_back(makeBuiltin(
        u"slide-copy"_ustr, u"幻灯文案"_ustr, u"impress"_ustr, u"impress"_ustr, u"rewrite"_ustr,
        u"/幻灯文案"_ustr,
        u"【幻灯文案】压缩为演讲友好短句，每行一要点。\n原文：\n{selection}"_ustr, 220));
    v.push_back(makeBuiltin(
        u"speaker-notes"_ustr, u"讲稿备注"_ustr, u"impress"_ustr, u"impress"_ustr, u"chat"_ustr,
        u"/讲稿"_ustr,
        u"【讲稿】为下列要点写 30–60 秒口播稿。\n要点：\n{selection}"_ustr, 230));
    v.push_back(makeBuiltin(
        u"theme-layout-deck"_ustr, u"版式风格方案"_ustr, u"impress"_ustr, u"impress"_ustr,
        u"chat"_ustr, u"/版式方案"_ustr,
        u"【版式/风格多方案 · 非黑盒成片】给出 3 套版式+主题组合（方案A/B/C），\n"
        u"每套：主题名、首页/正文/结尾版式建议、配色（商务蓝/简洁灰等）、适用场合。\n"
        u"不要输出 ## 写回体；用户选定后再走「③ 选一写回」。\n"
        u"素材：\n{selection}"_ustr,
        215));
    v.back().options.autoSubmit = false;
    v.push_back(makeBuiltin(
        u"image-suggest"_ustr, u"配图建议"_ustr, u"impress"_ustr, u"impress"_ustr, u"chat"_ustr,
        u"/配图"_ustr,
        u"【配图建议 · 本地占位】为当前页/素材给出 2–4 条配图建议。\n"
        u"每条一行：配图：简洁中文画面描述（无外链、不生成真实图片）。\n"
        u"可附「版式：标题内容」。写回后成为幻灯占位框，用户可替换为真实图片。\n"
        u"素材：\n{selection}"_ustr,
        235));

    // —— General / multi-step / local RAG ——
    v.push_back(makeBuiltin(
        u"analyze-screenshot"_ustr, u"分析截图"_ustr, u"general"_ustr, u"any"_ustr, u"chat"_ustr,
        u"/分析截图"_ustr,
        u"【分析截图】用户附带了本地截图路径（@截图:…）。请根据路径旁说明与可见上下文："
        u"描述界面/表格/错误信息；给出可执行下一步（不编造看不见的细节）。\n"
        u"素材：\n{selection}"_ustr,
        285));
    v.push_back(makeBuiltin(
        u"ask-document"_ustr, u"问本文档"_ustr, u"general"_ustr, u"any"_ustr, u"chat"_ustr,
        u"/问本文档"_ustr,
        u"【问本文档 · 本地检索】仅根据当前打开文档回答，禁止编造文档中不存在的内容；"
        u"引用要点时标明位置（段落/单元格/幻灯）。\n"
        u"问题：\n{selection}"_ustr,
        290));
    v.back().options.pinned = true;
    v.back().options.includeDocContext = true;
    v.push_back(makeBuiltin(
        u"chart-assist"_ustr, u"图表助手"_ustr, u"calc"_ustr, u"calc"_ustr, u"plan"_ustr,
        u"/图表"_ustr,
        u"【图表助手】根据选区建议图表。要求：\n"
        u"1) 推荐图表类型（柱/线/饼/组合）与理由；\n"
        u"2) 指出数据系列、分类轴、是否需要合计行；\n"
        u"3) 给出插入步骤（用户可批准后用「插入图表」命令）；\n"
        u"4) 若需辅助列/公式，单独一行输出 = 公式。\n"
        u"选区：\n{selection}"_ustr,
        125));
    v.push_back(makeBuiltin(
        u"slide-page-edit"_ustr, u"本页改写"_ustr, u"impress"_ustr, u"impress"_ustr,
        u"rewrite"_ustr, u"/本页"_ustr,
        u"【页级指令】只改当前幻灯页文案（标题+要点），保持页数不变。\n"
        u"输出整页替换文本：第一行标题，其后每行一个要点；可附「讲稿：」。\n"
        u"当前页/选区：\n{selection}"_ustr,
        225));
    v.push_back(makeBuiltin(
        u"multi-step-agent"_ustr, u"多步协作"_ustr, u"general"_ustr, u"any"_ustr, u"agent"_ustr,
        u"/多步"_ustr,
        u"【多步协作】规划→执行→审查完成下列目标（禁止暗改主文档）。\n目标：\n{selection}"_ustr,
        300, /*agent*/ true));
    v.back().options.pinned = true;
    v.push_back(makeBuiltin(
        u"explain-selection"_ustr, u"解释选区"_ustr, u"general"_ustr, u"any"_ustr, u"chat"_ustr,
        u"/解释"_ustr,
        u"【解释】解释下列内容的含义、结构与可改进点。\n内容：\n{selection}"_ustr, 310));
    v.push_back(makeBuiltin(
        u"checklist-review"_ustr, u"清单审查"_ustr, u"general"_ustr, u"any"_ustr, u"review"_ustr,
        u"/清单审查"_ustr,
        u"【清单审查】用检查清单评估完整性/风险/表述，输出通过项与待改项。\n内容：\n{selection}"_ustr,
        320));

    // —— 业务场景包 v1（飞书应用目录 → Office 模板工厂；非多维表运行时）——
    // 命名用「模板/台账/清单」，禁止「系统/平台」默认文案。requireApproval 已默认 true。
    auto bizWriter = [&](const OUString& id, const OUString& title, const OUString& slash,
                         const OUString& prompt, sal_Int32 order) {
        v.push_back(makeBuiltin(id, title, u"writer"_ustr, u"writer"_ustr, u"plan"_ustr, slash,
                                prompt, order));
        v.back().options.autoSubmit = false;
        v.back().options.attachSelection = true;
    };
    auto bizCalc = [&](const OUString& id, const OUString& title, const OUString& slash,
                       const OUString& prompt, sal_Int32 order) {
        v.push_back(makeBuiltin(id, title, u"calc"_ustr, u"calc"_ustr, u"plan"_ustr, slash, prompt,
                                order));
        v.back().options.autoSubmit = false;
        v.back().options.attachSelection = true;
    };
    auto bizImpress = [&](const OUString& id, const OUString& title, const OUString& slash,
                          const OUString& prompt, sal_Int32 order) {
        v.push_back(makeBuiltin(id, title, u"impress"_ustr, u"impress"_ustr, u"plan"_ustr, slash,
                                prompt, order));
        v.back().options.autoSubmit = false;
        v.back().options.attachSelection = true;
    };

    const OUString kCalcRules
        = u"【输出格式 · Calc 台账模板】\n"
          u"1) 第一行：表头（制表符或逗号分隔列名，中文）；\n"
          u"2) 2–4 行示例数据（可用【示例】前缀，勿编造真实隐私）；\n"
          u"3) 可选：单独列出以 = 开头的汇总公式；\n"
          u"4) 文末一行说明：「此为本地表格模板，非在线业务系统。」\n"
          u"主题/补充：\n{selection}"_ustr;
    const OUString kWriterRules
        = u"【输出格式 · Writer 公文/表单模板】\n"
          u"1) 标题 + 清晰分节；缺字段用【待填】；\n"
          u"2) 语气专业克制，不编造无法核实数据；\n"
          u"3) 只输出可粘贴进正文的成稿；\n"
          u"4) 文末注明：「本地文档模板，可导出 PDF。」\n"
          u"主题/补充：\n{selection}"_ustr;

    bizWriter(u"biz-daily-report"_ustr, u"团队工作日报"_ustr, u"/团队日报"_ustr,
              u"【团队工作日报模板】生成可填写日报骨架：日期/姓名/今日完成/风险阻塞/明日计划/"
              u"需协调事项。每人可复制一节。\n"_ustr
                  + kWriterRules,
              400);
    bizWriter(u"biz-weekly-report"_ustr, u"团队周报"_ustr, u"/团队周报"_ustr,
              u"【团队周报模板】本周目标、关键进展、数据、问题、下周计划、所需支持。\n"_ustr
                  + kWriterRules,
              401);
    bizCalc(u"biz-todo-board"_ustr, u"AI 待办清单"_ustr, u"/待办台账"_ustr,
            u"【待办事项台账】列：任务、优先级、负责人、状态、截止日、风险、备注。"
            u"状态用：未开始/进行中/阻塞/完成。\n"_ustr
                + kCalcRules,
            402);
    bizCalc(u"biz-project-tasks"_ustr, u"项目任务清单"_ustr, u"/项目任务"_ustr,
            u"【项目任务清单】列：模块、任务、负责人、开始、截止、进度%、依赖、状态、风险。\n"_ustr
                + kCalcRules,
            403);
    bizCalc(u"biz-expense"_ustr, u"费用报销单"_ustr, u"/报销单"_ustr,
            u"【费用报销台账】列：日期、报销人、部门、费用类型、摘要、金额、票据号、审批状态、"
            u"备注。费用类型示例：差旅/餐饮/办公/交通。\n"_ustr
                + kCalcRules,
            404);
    bizWriter(u"biz-leave"_ustr, u"请假申请"_ustr, u"/请假申请"_ustr,
              u"【请假申请单】字段：申请人、部门、请假类型、起止时间、天数、事由、代理人、"
              u"审批意见区。\n"_ustr
                  + kWriterRules,
              405);
    bizCalc(u"biz-crm-leads"_ustr, u"客户商机台账"_ustr, u"/客户台账"_ustr,
            u"【客户与商机台账 · 非 CRM 系统】列：客户名、联系人、电话、来源、阶段、预计金额、"
            u"下次跟进、负责人、备注。阶段：线索/需求/方案/谈判/赢单/丢单。\n"_ustr
                + kCalcRules,
            406);
    bizCalc(u"biz-orders"_ustr, u"订单台账"_ustr, u"/订单台账"_ustr,
            u"【订单台账】列：订单号、客户、产品/服务、数量、单价、金额、下单日、交付日、"
            u"回款状态、备注。\n"_ustr
                + kCalcRules,
            407);
    bizCalc(u"biz-inventory"_ustr, u"库存进出台账"_ustr, u"/库存台账"_ustr,
            u"【库存基础台账 · 非 WMS】Sheet 建议在说明中写出两表结构："
            u"①商品：SKU、名称、单位、安全库存；②流水：日期、SKU、类型(入/出)、数量、结存、经手人。"
            u"先输出①的表头+示例，再输出②。\n"_ustr
                + kCalcRules,
            408);
    bizCalc(u"biz-contracts"_ustr, u"合同台账"_ustr, u"/合同台账"_ustr,
            u"【合同台账】列：合同编号、相对方、类型、金额、签订日、生效、到期、负责人、状态、"
            u"续约提醒、备注。\n"_ustr
                + kCalcRules,
            409);
    bizWriter(u"biz-quote"_ustr, u"报价单"_ustr, u"/报价单"_ustr,
              u"【报价单正文模板】含：供方/需方、报价日期、有效期、明细表（品名数量单价金额）、"
              u"合计、付款与交付条款、签章区。明细用纯文本表格即可。\n"_ustr
                  + kWriterRules,
              410);
    bizCalc(u"biz-attendance"_ustr, u"考勤与请假台账"_ustr, u"/考勤台账"_ustr,
            u"【考勤台账】列：日期、姓名、部门、出勤、迟到、请假类型、备注。\n"_ustr
                + kCalcRules,
            411);
    bizCalc(u"biz-recruit"_ustr, u"招聘进度表"_ustr, u"/招聘进度"_ustr,
            u"【招聘进度】列：岗位、候选人、渠道、阶段、面试官、结果、下步动作、备注。"
            u"阶段：简历/初筛/面试/Offer/入职/淘汰。\n"_ustr
                + kCalcRules,
            412);
    bizCalc(u"biz-okr"_ustr, u"OKR 进度表"_ustr, u"/OKR表"_ustr,
            u"【OKR 进度】列：周期、目标O、关键结果KR、负责人、进度%、状态、风险、备注。\n"_ustr
                + kCalcRules,
            413);
    bizWriter(u"biz-meeting-signup"_ustr, u"活动签到表说明+表头"_ustr, u"/活动签到"_ustr,
              u"【活动签到】先给简短活动说明（名称/时间/地点），再给表格列："
              u"序号、姓名、单位、手机、签到时间、备注。\n"_ustr
                  + kWriterRules,
              414);
    bizCalc(u"biz-survey"_ustr, u"满意度调研表"_ustr, u"/满意度调研"_ustr,
            u"【客户满意度调研】列：时间、客户、评分1-5、维度(产品/服务/交付)、反馈原文、跟进人、"
            u"改进项。\n"_ustr
                + kCalcRules,
            415);
    bizCalc(u"biz-assets"_ustr, u"固定资产台账"_ustr, u"/固定资产"_ustr,
            u"【固定资产台账】列：资产编号、名称、类别、购入日、原值、使用人、部门、状态、位置、"
            u"备注。\n"_ustr
                + kCalcRules,
            416);
    bizCalc(u"biz-invoice"_ustr, u"发票与费用台账"_ustr, u"/发票台账"_ustr,
            u"【发票台账 · 无税控验真】列：开票日、发票号、购销方、税额、价税合计、关联报销单、"
            u"入账状态、备注。\n"_ustr
                + kCalcRules,
            417);
    bizCalc(u"biz-tickets"_ustr, u"工单台账"_ustr, u"/工单台账"_ustr,
            u"【工单台账 · 非自动派单系统】列：工单号、类型、标题、优先级、提单人、处理人、状态、"
            u"创建日、解决日、备注。\n"_ustr
                + kCalcRules,
            418);
    bizWriter(u"biz-contract-body"_ustr, u"购销合同正文"_ustr, u"/购销合同"_ustr,
              u"【购销合同正文骨架】甲乙方、标的、数量价款、交付、验收、付款、违约、争议、签章。"
              u"法律条款用通用占位，标注需法务审定。\n"_ustr
                  + kWriterRules,
              419);
    bizImpress(u"biz-ops-review"_ustr, u"经营复盘演示"_ustr, u"/经营复盘"_ustr,
               u"【经营复盘演示】生成 6–10 页：封面、核心指标、结构、趋势、问题、动作、风险、"
               u"下一步。每页：## N. 标题 / 版式 / 要点 / 讲稿。\n主题/素材：\n{selection}"_ustr,
               420);
    bizCalc(u"biz-sales-daily"_ustr, u"每日销售额汇总"_ustr, u"/日销售"_ustr,
            u"【每日销售额】列：日期、渠道/门店、订单数、销售额、退款、净额、备注；并给合计公式。"
            u"\n"_ustr
                + kCalcRules,
            421);
    bizCalc(u"biz-member"_ustr, u"会员信息表"_ustr, u"/会员表"_ustr,
            u"【会员信息】列：会员号、姓名、手机、等级、开卡日、余额/积分、兴趣标签、备注。\n"_ustr
                + kCalcRules,
            422);
    bizWriter(u"biz-personal-plan"_ustr, u"月度个人计划"_ustr, u"/月度计划"_ustr,
              u"【月度个人计划】目标、周拆解、关键任务、复盘问题、习惯打卡区。\n"_ustr
                  + kWriterRules,
              423);
    bizCalc(u"biz-personal-ledger"_ustr, u"个人记账"_ustr, u"/个人记账"_ustr,
            u"【个人记账】列：日期、类别、收支、金额、账户、备注；类别示例：餐饮/交通/住房/工资。"
            u"\n"_ustr
                + kCalcRules,
            424);
    bizWriter(u"biz-reading-notes"_ustr, u"读书笔记"_ustr, u"/读书笔记"_ustr,
              u"【读书笔记】书名/作者、核心观点、金句、启发、行动项。\n"_ustr + kWriterRules,
              425);
    bizCalc(u"biz-bug-list"_ustr, u"需求与缺陷清单"_ustr, u"/缺陷清单"_ustr,
            u"【需求/缺陷清单】列：ID、类型(需求/缺陷)、标题、优先级、状态、负责人、发现日、"
            u"目标版本、备注。\n"_ustr
                + kCalcRules,
            426);
    bizWriter(u"biz-offer-letter-stub"_ustr, u"证明开具底稿"_ustr, u"/在职证明"_ustr,
              u"【在职/收入证明底稿】含抬头、被证明人信息、【待填】薪资与职务、用途、落款公章区。"
              u"注明仅供模板，内容须人工核实。\n"_ustr
                  + kWriterRules,
              427);

    return v;
}

OUString DocumentAIScenarioStore::serializeJson(const ScenarioCatalog& rCatalog)
{
    OUStringBuffer b;
    b.append(u"{\n");
    b.append(u"  \"schema_version\": \"v1-scenarios\",\n");
    b.append(u"  \"items\": [\n");
    for (size_t i = 0; i < rCatalog.items.size(); ++i)
    {
        const DocumentAIScenario& s = rCatalog.items[i];
        if (i)
            b.append(u",\n");
        b.append(u"  {\n");
        appendJsonString(b, u"id", s.id);
        b.append(u",\n");
        appendJsonString(b, u"titleZh", s.titleZh);
        b.append(u",\n");
        appendJsonString(b, u"category", s.category);
        b.append(u",\n");
        appendJsonString(b, u"preferredSurface", s.preferredSurface);
        b.append(u",\n");
        appendJsonString(b, u"capabilityHint", s.capabilityHint);
        b.append(u",\n");
        appendJsonString(b, u"slashCommand", s.slashCommand);
        b.append(u",\n");
        appendJsonString(b, u"promptTemplate", s.promptTemplate);
        b.append(u",\n");
        appendJsonInt(b, u"sortOrder", s.sortOrder);
        b.append(u",\n");
        appendJsonBool(b, u"builtin", s.builtin);
        b.append(u",\n");
        appendJsonBool(b, u"enabled", s.enabled);
        b.append(u",\n");
        appendJsonBool(b, u"opt_attachSelection", s.options.attachSelection);
        b.append(u",\n");
        appendJsonBool(b, u"opt_includeDocContext", s.options.includeDocContext);
        b.append(u",\n");
        appendJsonBool(b, u"opt_autoSubmit", s.options.autoSubmit);
        b.append(u",\n");
        appendJsonBool(b, u"opt_useAgentPipeline", s.options.useAgentPipeline);
        b.append(u",\n");
        appendJsonBool(b, u"opt_requireApproval", s.options.requireApproval);
        b.append(u",\n");
        appendJsonBool(b, u"opt_showAsButton", s.options.showAsButton);
        b.append(u",\n");
        appendJsonBool(b, u"opt_pinned", s.options.pinned);
        b.append(u"\n  }");
    }
    b.append(u"\n  ]\n}\n");
    return b.makeStringAndClear();
}

ScenarioCatalog DocumentAIScenarioStore::parseJson(const OUString& rJson)
{
    ScenarioCatalog cat;
    cat.schemaVersion = u"v1-scenarios"_ustr;
    if (rJson.isEmpty())
        return cat;

    // Split on "\"id\"" occurrences for each item object (simple linear parse).
    sal_Int32 search = 0;
    while (true)
    {
        sal_Int32 idPos = rJson.indexOf(u"\"id\""_ustr, search);
        if (idPos < 0)
            break;
        // Find object start
        sal_Int32 objStart = idPos;
        while (objStart > 0 && rJson[objStart] != u'{')
            --objStart;
        sal_Int32 depth = 0;
        sal_Int32 objEnd = objStart;
        for (; objEnd < rJson.getLength(); ++objEnd)
        {
            if (rJson[objEnd] == u'{')
                ++depth;
            else if (rJson[objEnd] == u'}')
            {
                --depth;
                if (depth == 0)
                {
                    ++objEnd;
                    break;
                }
            }
        }
        if (objEnd <= objStart)
            break;
        const OUString frag = rJson.copy(objStart, objEnd - objStart);
        DocumentAIScenario s;
        s.id = jsonStringField(frag, u"id");
        if (s.id.isEmpty())
        {
            search = objEnd;
            continue;
        }
        s.titleZh = jsonStringField(frag, u"titleZh");
        s.category = jsonStringField(frag, u"category");
        s.preferredSurface = jsonStringField(frag, u"preferredSurface");
        s.capabilityHint = jsonStringField(frag, u"capabilityHint");
        s.slashCommand = jsonStringField(frag, u"slashCommand");
        s.promptTemplate = jsonStringField(frag, u"promptTemplate");
        s.sortOrder = jsonIntField(frag, u"sortOrder", 100);
        s.builtin = jsonBoolField(frag, u"builtin", false);
        s.enabled = jsonBoolField(frag, u"enabled", true);
        s.options.attachSelection = jsonBoolField(frag, u"opt_attachSelection", true);
        s.options.includeDocContext = jsonBoolField(frag, u"opt_includeDocContext", true);
        s.options.autoSubmit = jsonBoolField(frag, u"opt_autoSubmit", true);
        s.options.useAgentPipeline = jsonBoolField(frag, u"opt_useAgentPipeline", false);
        s.options.requireApproval = jsonBoolField(frag, u"opt_requireApproval", true);
        s.options.showAsButton = jsonBoolField(frag, u"opt_showAsButton", true);
        s.options.pinned = jsonBoolField(frag, u"opt_pinned", false);
        cat.items.push_back(s);
        search = objEnd;
    }
    return cat;
}

void mergeMissingBuiltins(ScenarioCatalog& cat)
{
    const auto builtins = DocumentAIScenarioStore::builtinDefaults();
    for (const auto& b : builtins)
    {
        if (!DocumentAIScenarioStore::find(cat, b.id))
            cat.items.push_back(b);
    }
    std::stable_sort(cat.items.begin(), cat.items.end(),
                     DocumentAIScenarioStore::lessByPinThenOrder);
}

bool DocumentAIScenarioStore::lessByPinThenOrder(const DocumentAIScenario& a,
                                                 const DocumentAIScenario& b)
{
    if (a.options.pinned != b.options.pinned)
        return a.options.pinned && !b.options.pinned;
    return a.sortOrder < b.sortOrder;
}

ScenarioCatalog DocumentAIScenarioStore::load()
{
    ScenarioCatalog cat;
    ensureDefaultTemplate();
    const OUString path = defaultConfigPath();
    if (!path.isEmpty())
    {
        const OUString body = readFileUtf8(path);
        if (!body.isEmpty())
            cat = parseJson(body);
    }
    if (cat.items.empty())
        cat.items = builtinDefaults();
    else
        mergeMissingBuiltins(cat);
    // Upgrade path: older configs without opt_pinned → seed a few favorites
    // only when nothing is pinned yet (do not override explicit user choices).
    bool anyPinned = false;
    for (const auto& s : cat.items)
    {
        if (s.options.pinned)
        {
            anyPinned = true;
            break;
        }
    }
    if (!anyPinned)
    {
        for (auto& s : cat.items)
        {
            if (s.id == u"official-polish"_ustr || s.id == u"formula-assist"_ustr
                || s.id == u"outline-to-slides"_ustr || s.id == u"multi-step-agent"_ustr)
                s.options.pinned = true;
        }
    }
    cat.schemaVersion = u"v1-scenarios"_ustr;
    return cat;
}

bool DocumentAIScenarioStore::ensureDefaultTemplate()
{
    const OUString path = defaultConfigPath();
    if (path.isEmpty())
        return false;
    const sal_Int32 slash = path.lastIndexOf(u'/');
    if (slash > 0)
    {
        const OUString dir = path.copy(0, slash);
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(dir, dirUrl) == osl::FileBase::E_None)
            osl::Directory::createPath(dirUrl);
    }
    std::ifstream exists(OUStringToOString(path, RTL_TEXTENCODING_UTF8).getStr());
    if (exists.good())
        return true;
    ScenarioCatalog cat;
    cat.schemaVersion = u"v1-scenarios"_ustr;
    cat.items = builtinDefaults();
    return writeFileUtf8(path, std::string(OUStringToOString(serializeJson(cat), RTL_TEXTENCODING_UTF8)));
}

bool DocumentAIScenarioStore::save(const ScenarioCatalog& rCatalog)
{
    ensureDefaultTemplate();
    const OUString path = defaultConfigPath();
    if (path.isEmpty())
        return false;
    const sal_Int32 slash = path.lastIndexOf(u'/');
    if (slash > 0)
    {
        const OUString dir = path.copy(0, slash);
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(dir, dirUrl) == osl::FileBase::E_None)
            osl::Directory::createPath(dirUrl);
    }
    ScenarioCatalog toSave = rCatalog;
    toSave.schemaVersion = u"v1-scenarios"_ustr;
    return writeFileUtf8(
        path, std::string(OUStringToOString(serializeJson(toSave), RTL_TEXTENCODING_UTF8)));
}

bool DocumentAIScenarioStore::upsert(ScenarioCatalog& rCatalog, const DocumentAIScenario& rItem)
{
    if (rItem.id.isEmpty() || rItem.titleZh.isEmpty())
        return false;
    if (auto* p = findMutable(rCatalog, rItem.id))
    {
        const bool wasBuiltin = p->builtin;
        *p = rItem;
        if (wasBuiltin)
            p->builtin = true; // cannot strip builtin flag
        return true;
    }
    rCatalog.items.push_back(rItem);
    return true;
}

bool DocumentAIScenarioStore::removeById(ScenarioCatalog& rCatalog, const OUString& rId)
{
    for (auto it = rCatalog.items.begin(); it != rCatalog.items.end(); ++it)
    {
        if (it->id != rId)
            continue;
        if (it->builtin)
        {
            // Soft-delete builtins: disable + hide button
            it->enabled = false;
            it->options.showAsButton = false;
            return true;
        }
        rCatalog.items.erase(it);
        return true;
    }
    return false;
}

DocumentAIScenario* DocumentAIScenarioStore::findMutable(ScenarioCatalog& rCatalog,
                                                         const OUString& rId)
{
    for (auto& s : rCatalog.items)
        if (s.id == rId)
            return &s;
    return nullptr;
}

const DocumentAIScenario* DocumentAIScenarioStore::find(const ScenarioCatalog& rCatalog,
                                                        const OUString& rId)
{
    for (const auto& s : rCatalog.items)
        if (s.id == rId)
            return &s;
    return nullptr;
}

std::vector<DocumentAIScenario>
DocumentAIScenarioStore::listExecutableButtons(const ScenarioCatalog& rCatalog)
{
    return listExecutableButtonsForSurface(rCatalog, OUString());
}

std::vector<DocumentAIScenario>
DocumentAIScenarioStore::listExecutableButtonsForSurface(const ScenarioCatalog& rCatalog,
                                                         const OUString& rSurfaceFilter)
{
    const OUString filter = rSurfaceFilter.toAsciiLowerCase().trim();
    const bool filterAll
        = filter.isEmpty() || filter == u"any"_ustr || filter == u"all"_ustr
          || filter == u"none"_ustr || filter == u"unknown"_ustr;

    std::vector<DocumentAIScenario> out;
    for (const auto& s : rCatalog.items)
    {
        if (!s.enabled || !s.options.showAsButton)
            continue;
        if (!filterAll)
        {
            const OUString pref = s.preferredSurface.toAsciiLowerCase().trim();
            // also accept category match when preferredSurface empty
            const OUString cat = s.category.toAsciiLowerCase().trim();
            const bool ok = pref.isEmpty() || pref == u"any"_ustr || pref == filter
                            || cat == filter || cat == u"general"_ustr;
            if (!ok)
                continue;
        }
        out.push_back(s);
    }
    std::stable_sort(out.begin(), out.end(), lessByPinThenOrder);
    return out;
}

std::vector<DocumentAIScenario>
DocumentAIScenarioStore::listExecutableButtonsForCategory(const ScenarioCatalog& rCatalog,
                                                          const OUString& rCategory)
{
    const OUString want = rCategory.toAsciiLowerCase().trim();
    std::vector<DocumentAIScenario> out;
    for (const auto& s : rCatalog.items)
    {
        if (!s.enabled || !s.options.showAsButton)
            continue;
        const OUString cat = s.category.toAsciiLowerCase().trim();
        const OUString pref = s.preferredSurface.toAsciiLowerCase().trim();
        bool ok = false;
        if (want == u"general"_ustr || want.isEmpty())
        {
            ok = cat == u"general"_ustr || cat.isEmpty() || pref == u"any"_ustr
                 || (cat != u"writer"_ustr && cat != u"calc"_ustr && cat != u"impress"_ustr
                     && pref != u"writer"_ustr && pref != u"calc"_ustr && pref != u"impress"_ustr);
        }
        else
        {
            ok = cat == want || pref == want;
        }
        if (ok)
            out.push_back(s);
    }
    std::stable_sort(out.begin(), out.end(), lessByPinThenOrder);
    return out;
}

std::vector<DocumentAIScenario>
DocumentAIScenarioStore::listPinnedButtons(const ScenarioCatalog& rCatalog)
{
    std::vector<DocumentAIScenario> out;
    for (const auto& s : rCatalog.items)
    {
        if (!s.enabled || !s.options.showAsButton || !s.options.pinned)
            continue;
        out.push_back(s);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const DocumentAIScenario& a, const DocumentAIScenario& b) {
                         return a.sortOrder < b.sortOrder;
                     });
    return out;
}

sal_Int32 DocumentAIScenarioStore::countExecutableButtonsForCategory(const ScenarioCatalog& rCatalog,
                                                                      const OUString& rCategory)
{
    return static_cast<sal_Int32>(listExecutableButtonsForCategory(rCatalog, rCategory).size());
}

bool DocumentAIScenarioStore::setPinned(ScenarioCatalog& rCatalog, const OUString& rId, bool bPinned)
{
    auto* p = findMutable(rCatalog, rId);
    if (!p)
        return false;
    p->options.pinned = bPinned;
    return true;
}

void DocumentAIScenarioStore::renumberSortOrders(ScenarioCatalog& rCatalog)
{
    // Renumber preserves relative order by sortOrder only (pin is orthogonal).
    std::stable_sort(rCatalog.items.begin(), rCatalog.items.end(),
                     [](const DocumentAIScenario& a, const DocumentAIScenario& b) {
                         return a.sortOrder < b.sortOrder;
                     });
    sal_Int32 order = 10;
    for (auto& s : rCatalog.items)
    {
        s.sortOrder = order;
        order += 10;
    }
}

bool DocumentAIScenarioStore::moveUp(ScenarioCatalog& rCatalog, const OUString& rId)
{
    renumberSortOrders(rCatalog);
    for (size_t i = 0; i < rCatalog.items.size(); ++i)
    {
        if (rCatalog.items[i].id != rId)
            continue;
        if (i == 0)
            return false;
        std::swap(rCatalog.items[i].sortOrder, rCatalog.items[i - 1].sortOrder);
        renumberSortOrders(rCatalog);
        return true;
    }
    return false;
}

bool DocumentAIScenarioStore::moveDown(ScenarioCatalog& rCatalog, const OUString& rId)
{
    renumberSortOrders(rCatalog);
    for (size_t i = 0; i < rCatalog.items.size(); ++i)
    {
        if (rCatalog.items[i].id != rId)
            continue;
        if (i + 1 >= rCatalog.items.size())
            return false;
        std::swap(rCatalog.items[i].sortOrder, rCatalog.items[i + 1].sortOrder);
        renumberSortOrders(rCatalog);
        return true;
    }
    return false;
}

bool DocumentAIScenarioStore::reorderByIds(ScenarioCatalog& rCatalog,
                                           const std::vector<OUString>& rOrderedIds)
{
    if (rOrderedIds.empty() || rCatalog.items.empty())
        return false;

    std::vector<DocumentAIScenario> ordered;
    ordered.reserve(rCatalog.items.size());
    for (const auto& id : rOrderedIds)
    {
        if (auto* p = findMutable(rCatalog, id))
        {
            // Move out by id (skip duplicates)
            bool already = false;
            for (const auto& o : ordered)
            {
                if (o.id == id)
                {
                    already = true;
                    break;
                }
            }
            if (!already)
                ordered.push_back(*p);
        }
    }
    // Append remaining items not in the ordered list (stable)
    for (const auto& s : rCatalog.items)
    {
        bool found = false;
        for (const auto& o : ordered)
        {
            if (o.id == s.id)
            {
                found = true;
                break;
            }
        }
        if (!found)
            ordered.push_back(s);
    }
    rCatalog.items = std::move(ordered);
    renumberSortOrders(rCatalog);
    return true;
}

OUString DocumentAIScenarioStore::pendingRunPath()
{
    OUString overridePath = envOrEmpty("KQOFFICE_AI_PENDING_SCENARIO");
    if (!overridePath.isEmpty())
        return overridePath;
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return OUString();
    return OUString::fromUtf8(home) + u"/.config/kqoffice/pending-scenario-run"_ustr;
}

bool DocumentAIScenarioStore::queuePendingRun(const OUString& rScenarioId)
{
    const OUString id = rScenarioId.trim();
    if (id.isEmpty())
        return false;
    const OUString path = pendingRunPath();
    if (path.isEmpty())
        return false;
    const sal_Int32 slash = path.lastIndexOf(u'/');
    if (slash > 0)
    {
        const OUString dir = path.copy(0, slash);
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(dir, dirUrl) == osl::FileBase::E_None)
            osl::Directory::createPath(dirUrl);
    }
    return writeFileUtf8(path, std::string(OUStringToOString(id, RTL_TEXTENCODING_UTF8)));
}

OUString DocumentAIScenarioStore::takePendingRun()
{
    // Env wins once, then file.
    OUString fromEnv = envOrEmpty("KQOFFICE_AI_RUN_SCENARIO");
    if (!fromEnv.isEmpty())
    {
        // Clear for process: setenv empty not portable — only consume file for repeat.
        // Env is one-shot: caller should unset; we still return it once per take if set.
        unsetenv("KQOFFICE_AI_RUN_SCENARIO");
        return fromEnv.trim();
    }
    const OUString path = pendingRunPath();
    if (path.isEmpty())
        return OUString();
    const OUString body = readFileUtf8(path).trim();
    if (!body.isEmpty())
    {
        // Truncate file after take
        writeFileUtf8(path, std::string());
    }
    return body;
}

OUString DocumentAIScenarioStore::expandPrompt(const DocumentAIScenario& rScenario,
                                               const OUString& rSelectionText)
{
    OUString tmpl = rScenario.promptTemplate;
    if (tmpl.isEmpty())
        return OUString();
    const OUString needle = u"{selection}"_ustr;
    const sal_Int32 pos = tmpl.indexOf(needle);
    if (pos < 0)
        return tmpl;
    OUString body = rSelectionText;
    if (!rScenario.options.attachSelection || body.isEmpty())
        body = u"（当前无选区：请结合整篇/当前对象上下文）"_ustr;
    return tmpl.replaceAt(pos, needle.getLength(), body);
}

// C ABI for automation / scripts
extern "C" void kqoffice_ai_queue_scenario_run(const char* pUtf8Id)
{
    if (!pUtf8Id || !*pUtf8Id)
        return;
    kqoffice::ai::chat::DocumentAIScenarioStore::queuePendingRun(OUString::fromUtf8(pUtf8Id));
}

const DocumentAIScenario* DocumentAIScenarioStore::matchSlash(const ScenarioCatalog& rCatalog,
                                                              const OUString& rUserInput)
{
    const OUString t = rUserInput.trim();
    for (const auto& s : rCatalog.items)
    {
        if (!s.enabled || s.slashCommand.isEmpty())
            continue;
        if (t == s.slashCommand || t.startsWith(OUString(s.slashCommand + u" "_ustr))
            || t == s.id)
            return &s;
    }
    return nullptr;
}

// —— Legacy facade ——
std::vector<DocumentAIScenario> DocumentAIScenarios::all()
{
    return DocumentAIScenarioStore::load().items;
}

DocumentAIScenario DocumentAIScenarios::findBySlashOrId(const OUString& rToken)
{
    const ScenarioCatalog cat = DocumentAIScenarioStore::load();
    if (const DocumentAIScenario* p = DocumentAIScenarioStore::matchSlash(cat, rToken))
        return *p;
    if (const DocumentAIScenario* p = DocumentAIScenarioStore::find(cat, rToken))
        return *p;
    // English aliases
    if (rToken.startsWith(u"/polish"_ustr))
        return findBySlashOrId(u"/公文润色"_ustr);
    if (rToken.startsWith(u"/formula"_ustr))
        return findBySlashOrId(u"/公式助手"_ustr);
    if (rToken.startsWith(u"/outline"_ustr))
        return findBySlashOrId(u"/大纲成片"_ustr);
    return {};
}

OUString DocumentAIScenarios::expandPrompt(const DocumentAIScenario& rScenario,
                                           const OUString& rSelectionText)
{
    return DocumentAIScenarioStore::expandPrompt(rScenario, rSelectionText);
}

bool DocumentAIScenarios::isScenarioSlash(const OUString& rUserInput)
{
    const ScenarioCatalog cat = DocumentAIScenarioStore::load();
    return DocumentAIScenarioStore::matchSlash(cat, rUserInput) != nullptr;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
