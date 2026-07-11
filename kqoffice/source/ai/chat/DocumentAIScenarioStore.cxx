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

    // —— Impress ——
    v.push_back(makeBuiltin(
        u"outline-to-slides"_ustr, u"大纲成片"_ustr, u"impress"_ustr, u"impress"_ustr,
        u"plan"_ustr, u"/大纲成片"_ustr,
        u"【大纲成片 / 真成片结构】把素材拆成可写回的多页幻灯。要求：\n"
        u"1) 用编号幻灯，每页格式：\n"
        u"## 1. 标题\n"
        u"版式：标题内容|标题页|分栏|章节\n"
        u"主题：商务蓝|简洁灰（可选）\n"
        u"- 要点1\n- 要点2\n- 要点3\n"
        u"讲稿：30–60 秒口播（可选）\n"
        u"配图：建议画面描述（可选，写回为占位框）\n"
        u"2) 每页 3–5 要点；页数建议 5–10；\n"
        u"3) 首页用「版式：标题页」，章节分隔用「版式：章节」；\n"
        u"4) 写回 target 用 slide:N，new_text 含标题/版式/要点/讲稿/配图。\n"
        u"素材：\n{selection}"_ustr,
        210));
    v.back().options.pinned = true;
    v.push_back(makeBuiltin(
        u"blank-draft-impress"_ustr, u"AI 成片"_ustr, u"impress"_ustr, u"impress"_ustr,
        u"plan"_ustr, u"/AI成片"_ustr,
        u"【空白演示 AI 成片】按主题生成完整演示大纲并格式化成页。要求：\n"
        u"1) 首页标题页 + 目录/议程 + 正文页 + 总结/下一步；\n"
        u"2) 每页：\n## N. 标题\n- 要点\n讲稿：…\n"
        u"3) 语气商务专业；缺信息用【待填】。\n"
        u"主题/素材：\n{selection}"_ustr,
        205));
    v.back().options.autoSubmit = false;
    v.push_back(makeBuiltin(
        u"slide-copy"_ustr, u"幻灯文案"_ustr, u"impress"_ustr, u"impress"_ustr, u"rewrite"_ustr,
        u"/幻灯文案"_ustr,
        u"【幻灯文案】压缩为演讲友好短句，每行一要点。\n原文：\n{selection}"_ustr, 220));
    v.push_back(makeBuiltin(
        u"speaker-notes"_ustr, u"讲稿备注"_ustr, u"impress"_ustr, u"impress"_ustr, u"chat"_ustr,
        u"/讲稿"_ustr,
        u"【讲稿】为下列要点写 30–60 秒口播稿。\n要点：\n{selection}"_ustr, 230));
    v.push_back(makeBuiltin(
        u"theme-layout-deck"_ustr, u"版式主题成片"_ustr, u"impress"_ustr, u"impress"_ustr,
        u"plan"_ustr, u"/版式成片"_ustr,
        u"【版式+主题成片】生成完整演示并标注版式/主题。要求每页：\n"
        u"## N. 标题\n版式：…\n主题：商务蓝\n- 要点\n讲稿：…\n配图：…\n"
        u"首页标题页，中间标题内容或分栏，结尾总结页。\n素材：\n{selection}"_ustr,
        215));
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
