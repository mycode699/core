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

/// Attach Grok-style skill metadata (description + whenToUse + skillVersion).
void asSkill(DocumentAIScenario& s, const OUString& description, const OUString& whenToUse,
             sal_Int32 skillVer = 2)
{
    s.description = description;
    s.whenToUse = whenToUse;
    s.skillVersion = skillVer;
}

/// Shared trust-chain footer for every write-capable skill pack.
const OUString& skillTrustFooter()
{
    static const OUString k
        = u"\n## 信任链（硬约束）\n"
          u"- 本步只**提议**改动；主文档须用户点「批准写回」后才变。\n"
          u"- 一次批准只覆盖本批计划，不是永久授权后续静默改稿。\n"
          u"- 不编造原文没有的数据/文号/人名；缺信息用【待填】。\n"
          u"- 本地优先：不要求上传云端，不静默外联。\n"_ustr;
    return k;
}
} // namespace

sal_Int32 DocumentAIScenarioStore::skillPackVersion()
{
    // Factory skill-pack revision. Bump when quality-core skill bodies change so
    // load() can refresh persisted builtins without deleting the user's config.
    return 2;
}

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
    // —— Writer · Skill packs (Grok-style process: steps / rules / output / trust) ——
    v.push_back(makeBuiltin(
        u"official-polish"_ustr, u"公文润色"_ustr, u"writer"_ustr, u"writer"_ustr, u"rewrite"_ustr,
        u"/公文润色"_ustr,
        u"【Skill · 公文润色】庄重准确、删繁就简的高质量改稿。\n"
        u"## 何时使用\n"
        u"用户要润色通知/请示/报告/纪要/函，或说「更正式」「删废话」「公文体」。\n"
        u"## 步骤\n"
        u"1) 通读选区/正文，标出冗余、口语、歧义、缺失要素；\n"
        u"2) 输出 **3 条改动要点**（每条一句，说明改了什么、为何）；\n"
        u"3) 给出可整段粘贴的改写稿；\n"
        u"4) 若有明确句对替换，可附 ApplyPlan JSON（replace + target），未批准不写回。\n"
        u"## 硬规则\n"
        u"- 不编造数据/文号/人名/单位；缺信息用【待填】；\n"
        u"- 不改变事实与政策口径；不擅自升格/降格语气到戏谑；\n"
        u"- 优先删繁就简，避免堆砌四字空话。\n"
        u"## 输出格式\n"
        u"### 改动要点\n- …\n### 改写稿\n（可粘贴正文）\n"
        u"可选：```json {\"plan_id\":\"…\",\"operations\":[…]} ```\n"_ustr
            + skillTrustFooter() + u"## 素材\n{selection}"_ustr,
        10));
    asSkill(v.back(),
            u"庄重准确的公文润色：改动要点 + 可粘贴改写稿，不编造事实。"_ustr,
            u"公文润色|润色公文|通知润色|请示润色|正式一点|更正式|删繁就简|公文体|机关文|"
            u"庄重|公务文书|polish official"_ustr);
    v.back().options.pinned = true; // default 常用
    // —— 公文包（垂类）——
    v.push_back(makeBuiltin(
        u"official-notice"_ustr, u"通知公告"_ustr, u"writer"_ustr, u"writer"_ustr, u"plan"_ustr,
        u"/通知"_ustr,
        u"【通知公告】按机关公文习惯起草。结构：标题 / 主送 / 正文（事由-事项-要求）/ "
        u"落款日期。语气庄重；缺信息用【待填】。\n素材：\n{selection}"_ustr,
        12));
    asSkill(v.back(), u"起草通知/公告：标题主送正文落款，庄重不编造。"_ustr,
            u"通知|公告|发通知|写通知|通知公告|下发通知"_ustr);
    v.push_back(makeBuiltin(
        u"official-request"_ustr, u"请示函"_ustr, u"writer"_ustr, u"writer"_ustr, u"plan"_ustr,
        u"/请示"_ustr,
        u"【请示/函】写清：缘由、依据、具体请求、办结时限。一文一事。\n素材：\n{selection}"_ustr,
        13));
    asSkill(v.back(), u"起草请示或函：缘由依据请求时限，一文一事。"_ustr,
            u"请示|写请示|函|商请|报请|请示函"_ustr);
    v.push_back(makeBuiltin(
        u"official-summary"_ustr, u"工作总结"_ustr, u"writer"_ustr, u"writer"_ustr, u"plan"_ustr,
        u"/工作总结"_ustr,
        u"【工作总结】结构：总体概述 → 主要成绩（条列+数据）→ 问题不足 → 下阶段计划。\n"
        u"素材：\n{selection}"_ustr,
        14));
    asSkill(v.back(), u"工作总结骨架：成绩/问题/计划，数据不编造。"_ustr,
            u"工作总结|写总结|年度总结|季度总结|阶段总结"_ustr);
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
    asSkill(v.back(), u"公文要素清单+文种判定+可直接用改写稿。"_ustr,
            u"公文包|文种|要素检查|公文检查|公文清单"_ustr);
    v.back().options.pinned = true;
    v.push_back(makeBuiltin(
        u"rewrite-smooth"_ustr, u"通顺改写"_ustr, u"writer"_ustr, u"writer"_ustr, u"rewrite"_ustr,
        u"/通顺改写"_ustr,
        u"【Skill · 通顺改写】提升可读性与衔接，不改变事实与语气强度。\n"
        u"## 硬规则\n- 不编造；不升格/降格语气；保留专有名词。\n"
        u"## 输出\n可粘贴改写稿；可选 FIX| 仅当有明确句对。\n"_ustr
            + skillTrustFooter() + u"## 原文\n{selection}"_ustr,
        20));
    asSkill(v.back(), u"通顺改写：更好读，不改事实与语气强度。"_ustr,
            u"通顺|改通顺|读起来别扭|不通顺|rewrite smooth|理顺|改顺"_ustr);
    v.push_back(makeBuiltin(
        u"shorten"_ustr, u"精简压缩"_ustr, u"writer"_ustr, u"writer"_ustr, u"summarize"_ustr,
        u"/精简"_ustr,
        u"【Skill · 精简】保留核心信息，压缩到约一半篇幅。\n"
        u"## 硬规则\n- 不删关键数据/结论；不编造。\n"
        u"## 输出\n精简稿 + 可选 3 条删了什么。\n"_ustr
            + skillTrustFooter() + u"## 原文\n{selection}"_ustr,
        30));
    asSkill(v.back(), u"精简压缩到约一半，保留核心信息与数据。"_ustr,
            u"精简|缩短|压缩|太长了|短一点|再短|shorten|condense|缩写"_ustr);
    v.push_back(makeBuiltin(
        u"expand"_ustr, u"扩写丰富"_ustr, u"writer"_ustr, u"writer"_ustr, u"rewrite"_ustr,
        u"/扩写"_ustr,
        u"【Skill · 扩写】在不编造事实前提下补充细节与过渡。\n"
        u"## 硬规则\n- 缺信息用【待填】；不发明数据/案例。\n"
        u"## 输出\n扩写稿。\n"_ustr
            + skillTrustFooter() + u"## 原文\n{selection}"_ustr,
        40));
    asSkill(v.back(), u"扩写细节与过渡，不编造事实。"_ustr,
            u"扩写|写长一点|丰富一点|展开写|expand|加长|补充细节"_ustr);
    v.push_back(makeBuiltin(
        u"translate-en"_ustr, u"译为英文"_ustr, u"writer"_ustr, u"writer"_ustr, u"rewrite"_ustr,
        u"/译英"_ustr,
        u"【Skill · 译英】专业英文，保持格式与专有名词。\n"
        u"缺信息【TBD】；不擅自本地化品牌名。\n"_ustr
            + skillTrustFooter() + u"## 原文\n{selection}"_ustr,
        50));
    asSkill(v.back(), u"中文译专业英文，保持格式。"_ustr,
            u"译英|翻译成英文|英译|translate to english|英文版|翻成英文"_ustr);
    v.push_back(makeBuiltin(
        u"translate-zh"_ustr, u"译为中文"_ustr, u"writer"_ustr, u"writer"_ustr, u"rewrite"_ustr,
        u"/译中"_ustr,
        u"【Skill · 译中】简洁中文，保持格式。\n"_ustr
            + skillTrustFooter() + u"## 原文\n{selection}"_ustr,
        60));
    asSkill(v.back(), u"译为简洁中文，保持格式。"_ustr,
            u"译中|翻译成中文|中译|translate to chinese|中文版|翻成中文"_ustr);
    v.push_back(makeBuiltin(
        u"proofread"_ustr, u"校对审阅"_ustr, u"writer"_ustr, u"writer"_ustr, u"review"_ustr,
        u"/校对"_ustr,
        u"【Skill · 校对审阅】按严重度找错并给可写回 FIX。\n"
        u"## 何时使用\n"
        u"用户要校对、找错别字、标点、逻辑硬伤，或「帮我审一下」。\n"
        u"## 步骤\n"
        u"1) 通读；按 **严重 / 中等 / 轻微** 列问题；\n"
        u"2) 每条：位置线索 + 问题类型（错别字/标点/逻辑/语气）+ 改法；\n"
        u"3) 对可确定的替换，输出可写回块（未批准不改主文档）。\n"
        u"## 硬规则\n"
        u"- FIX 左半「原句片段」须能在原文中定位（足够长、勿截断关键词）；\n"
        u"- 拿不准的标「待人工确认」，不要硬改专有名词；\n"
        u"- 不借校对扩写或改立场。\n"
        u"## 输出格式\n"
        u"### 问题清单\n"
        u"- [严重] …\n"
        u"### 可写回（可选）\n"
        u"===可圈审阅修复===\n"
        u"FIX|原句片段|改正句\n"
        u"（每行一条）\n"_ustr
            + skillTrustFooter() + u"## 素材\n{selection}"_ustr,
        8));
    asSkill(v.back(),
            u"校对错别字/标点/逻辑，输出 FIX| 写回块，须批准才改主文档。"_ustr,
            u"校对|审阅|找错|错别字|标点|病句|proofread|review text|帮我审|"
            u"检查文字|改错"_ustr);
    // —— Writer · 内容/排版/设计质检（核心改稿面）——
    v.push_back(makeBuiltin(
        u"layout-polish"_ustr, u"排版优化"_ustr, u"writer"_ustr, u"writer"_ustr, u"plan"_ustr,
        u"/排版优化"_ustr,
        u"【Skill · 排版优化】只调结构与标题层级，不改写事实。\n"
        u"## 何时使用\n"
        u"标题层级乱、目录感差、列表不统一、段落过长、要「整理大纲/设标题样式」。\n"
        u"## 步骤\n"
        u"1) 诊断现状问题 ≤5 条（层级/列表/段长/跳级）；\n"
        u"2) 给出建议标题树（H1–H3）；\n"
        u"3) 输出可写回块（文字须与文档标题原文一致，便于软匹配定位）。\n"
        u"## 硬规则\n"
        u"- **不改写正文事实**，只提议标题层级/结构；\n"
        u"- H1| 后文字尽量等于文档中已有标题原文；定位不了只给建议，勿瞎写 para 号；\n"
        u"- 有段落号时优先 para:N|H1|标题。\n"
        u"## 输出格式\n"
        u"### 现状问题\n1) …\n### 建议标题树\n- H1 …\n### 可写回\n"
        u"===可圈大纲写回===\n"
        u"para:12|H1|标题原文\n"
        u"H1|标题原文\n"
        u"H2|小节原文\n"_ustr
            + skillTrustFooter() + u"## 素材\n{selection}"_ustr,
        6));
    asSkill(v.back(),
            u"标题层级与结构优化，输出可圈大纲写回块（H1|/para:），须批准。"_ustr,
            u"排版优化|排版|标题层级|层级乱|大纲写回|设标题|目录结构|layout|"
            u"typography|整理结构|标题样式|H1|H2"_ustr);
    v.back().options.pinned = true;
    v.push_back(makeBuiltin(
        u"content-quality"_ustr, u"内容质检"_ustr, u"writer"_ustr, u"writer"_ustr, u"review"_ustr,
        u"/内容质检"_ustr,
        u"【Skill · 内容质检】四维打分 + 最小改动建议（咨询为主，可附润色稿）。\n"
        u"## 何时使用\n"
        u"用户要质检、打分、查逻辑/冗余/语气，或「这篇写得怎么样」。\n"
        u"## 步骤\n"
        u"1) 四维 1–5 分：结构清晰 / 事实可核 / 语气得体 / 冗余控制；\n"
        u"2) Top 5 问题（位置线索 + 改法）；\n"
        u"3) 一版「最小改动」润色稿（可粘贴）；有明确句对时可附 FIX| 块。\n"
        u"## 硬规则\n"
        u"- 禁止编造原文没有的数据；\n"
        u"- 质检默认**不强制写回**；无 FIX/大纲块时以咨询收口；\n"
        u"- 打分要有一句依据，忌空泛「还可以」。\n"
        u"## 输出格式\n"
        u"### 总分与分项\n结构 x/5 · 事实 x/5 · 语气 x/5 · 冗余 x/5 · 总分\n"
        u"### Top 问题\n1) …\n### 最小改动稿\n…\n"_ustr
            + skillTrustFooter() + u"## 素材\n{selection}"_ustr,
        7));
    asSkill(v.back(),
            u"结构/事实/语气/冗余四维质检与最小改动稿，默认咨询不强制写回。"_ustr,
            u"内容质检|质检|打分|质量怎么样|写得怎么样|冗余|逻辑检查|"
            u"content quality|质量评估|文章质量"_ustr);
    v.back().options.pinned = true;
    v.push_back(makeBuiltin(
        u"doc-design-review"_ustr, u"版式设计审"_ustr, u"writer"_ustr, u"writer"_ustr, u"chat"_ustr,
        u"/版式设计审"_ustr,
        u"【Skill · 版式设计审 · 咨询】视觉层级建议，**不自动改样式**。\n"
        u"## 何时使用\n"
        u"页边距、标题对比、列表密度、表格可读性、页眉页脚、商务/汇报观感。\n"
        u"## 步骤\n"
        u"1) 快速诊断观感问题 3–5 条；\n"
        u"2) 给出 **3 套克制方案**：A 极简 / B 商务 / C 汇报；\n"
        u"3) 每套 **4 条**可执行设置建议（字号层级、间距、列表、表格）。\n"
        u"## 硬规则\n"
        u"- 本技能**只咨询**，不输出可圈大纲/FIX 写回块，不改主文档样式；\n"
        u"- 方案克制，避免花哨装饰建议。\n"
        u"## 输出格式\n"
        u"### 观感诊断\n…\n### 方案A 极简\n1)…\n### 方案B 商务\n…\n### 方案C 汇报\n…\n"_ustr
            + skillTrustFooter() + u"## 素材\n{selection}"_ustr,
        16));
    asSkill(v.back(),
            u"三套克制版式方案（咨询不改样式）：极简/商务/汇报。"_ustr,
            u"版式设计审|版式|视觉层级|页边距|页眉页脚|商务排版|设计审|"
            u"好看一点|排版观感"_ustr);
    v.push_back(makeBuiltin(
        u"minutes"_ustr, u"会议纪要"_ustr, u"writer"_ustr, u"writer"_ustr, u"summarize"_ustr,
        u"/会议纪要"_ustr,
        u"【Skill · 会议纪要】议题 / 结论 / 负责人 / 截止时间。\n"
        u"缺信息【待填】；不编造出席人与决议。\n"_ustr
            + skillTrustFooter() + u"## 素材\n{selection}"_ustr,
        80));
    asSkill(v.back(), u"会议纪要：议题结论负责人截止时间。"_ustr,
            u"会议纪要|纪要|会议记录|整理会议|minutes|开会记录"_ustr);
    v.push_back(makeBuiltin(
        u"report-structure"_ustr, u"汇报结构"_ustr, u"writer"_ustr, u"writer"_ustr, u"plan"_ustr,
        u"/汇报结构"_ustr,
        u"【Skill · 汇报结构】背景-进展-问题-计划-所需支持。\n"_ustr
            + skillTrustFooter() + u"## 素材\n{selection}"_ustr,
        90));
    asSkill(v.back(), u"汇报结构骨架：背景进展问题计划支持。"_ustr,
            u"汇报结构|汇报提纲|工作汇报|述职结构|汇报框架"_ustr);
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

    // —— Calc · Skill packs ——
    v.push_back(makeBuiltin(
        u"formula-assist"_ustr, u"公式助手"_ustr, u"calc"_ustr, u"calc"_ustr, u"chat"_ustr,
        u"/公式助手"_ustr,
        u"【Skill · 公式助手】为单元格/选区给出可写入公式（先公式后解释）。\n"
        u"## 何时使用\n"
        u"用户要写公式、求和、条件统计、比率、查找引用等。\n"
        u"## 步骤\n"
        u"1) 判断目标单元格与引用区域；\n"
        u"2) **第一行**输出以 = 开头的完整公式（单独一行）；\n"
        u"3) 说明用途与引用；可选 ApplyPlan / 可圈公式写回块。\n"
        u"## 硬规则\n"
        u"- 第一行必须是可写入公式；勿用中文全角＝；\n"
        u"- 不臆造表中不存在的列/区域；不确定标【待确认区域】；\n"
        u"- 写回前会走 dry-run；失败须用户二次确认。\n"
        u"## 输出格式\n"
        u"=SUM(A1:A10)\n"
        u"说明：…\n"
        u"可选：===可圈公式写回===\ncell:B2|=SUM(A1:A10)\n"_ustr
            + skillTrustFooter() + u"## 选区\n{selection}"_ustr,
        110));
    asSkill(v.back(),
            u"生成可写入公式（首行=公式），支持写回块与 dry-run。"_ustr,
            u"公式助手|公式|求和|SUM|AVERAGE|COUNTIF|写公式|formula|"
            u"单元格公式|怎么算"_ustr);
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
        u"【Skill · 数据清洗】问题清单 + 可写回清洗公式。\n"
        u"## 何时使用\n"
        u"空值、重复、空格、类型混乱、异常值、要 TRIM/去重/规范化。\n"
        u"## 步骤\n"
        u"1) 问题清单（类型 + **单元格引用**）；\n"
        u"2) 处理步骤（强调先备份）；\n"
        u"3) 可写回块（公式优先，未批准不改表）。\n"
        u"## 硬规则\n"
        u"- 每条问题尽量带 cell/range 引用；\n"
        u"- 不删除用户未要求删除的数据行（除非明确「去重删除」）；\n"
        u"- 清洗写回优先公式，便于撤销。\n"
        u"## 输出格式\n"
        u"### 问题\n1) A1 空值 …\n### 步骤\n…\n### 可写回\n"
        u"===可圈清洗写回===\n"
        u"cell:A1|=TRIM(A1)\n"_ustr
            + skillTrustFooter() + u"## 选区\n{selection}"_ustr,
        108));
    asSkill(v.back(),
            u"空值/重复/异常清洗清单 + 可圈清洗写回公式块。"_ustr,
            u"数据清洗|清洗|去空格|TRIM|去重|空值|异常值|clean data|规范化|"
            u"脏数据"_ustr);
    v.back().options.pinned = true;
    v.push_back(makeBuiltin(
        u"table-format"_ustr, u"表格美化"_ustr, u"calc"_ustr, u"calc"_ustr, u"plan"_ustr,
        u"/表格美化"_ustr,
        u"【Skill · 表格美化】表头/格式/冻结/打印可读性，不编造业务数据。\n"
        u"## 何时使用\n"
        u"表难看、表头不清、要冻结首行、数字格式、打印区域、条件格式建议。\n"
        u"## 步骤\n"
        u"1) 表头与冻结建议；\n"
        u"2) 数字/日期/百分比格式；\n"
        u"3) 列宽与打印；\n"
        u"4) 2–4 条校验/辅助公式（= 开头单独行）；可选写回块。\n"
        u"## 硬规则\n"
        u"- **不编造**业务数值；\n"
        u"- 建议可执行，忌空泛「调好看点」。\n"
        u"## 输出格式\n"
        u"### 表头与冻结\n…\n### 数字格式\n…\n### 公式\n=…\n"_ustr
            + skillTrustFooter() + u"## 选区\n{selection}"_ustr,
        109));
    asSkill(v.back(),
            u"表头/冻结/数字格式/打印与校验公式，提升表格可读性。"_ustr,
            u"表格美化|表头|冻结|冻结首行|数字格式|打印区域|表格排版|"
            u"table format|好看表格|列宽"_ustr);
    v.back().options.pinned = true;
    v.push_back(makeBuiltin(
        u"data-summary"_ustr, u"数据汇总"_ustr, u"calc"_ustr, u"calc"_ustr, u"summarize"_ustr,
        u"/数据汇总"_ustr,
        u"【Skill · 数据汇总】分布、极值、异常 + 可写公式（每行一个 =）。\n"
        u"## 硬规则\n- 异常带单元格引用；禁止臆造表中没有的数。\n"
        u"## 输出\n要点 + =公式行；可选 ===可圈公式写回===\n"_ustr
            + skillTrustFooter() + u"## 选区\n{selection}"_ustr,
        130));
    asSkill(v.back(), u"数据分布/极值/异常 + 汇总公式，不编造数。"_ustr,
            u"数据汇总|汇总|统计一下|分布|极值|aggregate|summarize data|算合计"_ustr);
    v.push_back(makeBuiltin(
        u"explain-cells"_ustr, u"解释单元格"_ustr, u"calc"_ustr, u"calc"_ustr, u"chat"_ustr,
        u"/解释单元格"_ustr,
        u"【Skill · 解释单元格】通俗解释选中含义与可能用途；不写回。\n"
        u"## 选区\n{selection}"_ustr,
        140));
    asSkill(v.back(), u"通俗解释选中单元格/区域含义。"_ustr,
            u"解释单元格|这格什么意思|解释公式|单元格含义|explain cell"_ustr);

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
    asSkill(v.back(), u"演示设计流①：只出大纲页序，禁止写回幻灯。"_ustr,
            u"先写大纲|演示大纲|ppt大纲|幻灯大纲|页序|design outline|做ppt大纲"_ustr);

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
    asSkill(v.back(), u"演示设计流②：3 套结构方案对比，不写回。"_ustr,
            u"多方案|方案对比|三套方案|design variants|方案A|方案B"_ustr);

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
    asSkill(v.back(), u"演示设计流③：选定方案→可写回 ## 页结构，须批准。"_ustr,
            u"选一写回|写回幻灯|生成幻灯|大纲成片|design apply|做成幻灯片"_ustr);

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
    asSkill(v.back(), u"导出 PPTX 操作指引（不执行导出）。"_ustr,
            u"导出PPTX|导出pptx|导出演示|export pptx|另存pptx"_ustr);

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
        u"【Skill · 幻灯文案】压缩为演讲友好短句，每行一要点。\n"_ustr
            + skillTrustFooter() + u"## 原文\n{selection}"_ustr,
        220));
    asSkill(v.back(), u"幻灯文案压成短句要点。"_ustr,
            u"幻灯文案|幻灯片文案|要点改短|slide copy|页文案"_ustr);
    v.push_back(makeBuiltin(
        u"speaker-notes"_ustr, u"讲稿备注"_ustr, u"impress"_ustr, u"impress"_ustr, u"chat"_ustr,
        u"/讲稿"_ustr,
        u"【Skill · 讲稿】30–60 秒口播稿。\n"_ustr
            + skillTrustFooter() + u"## 要点\n{selection}"_ustr,
        230));
    asSkill(v.back(), u"为要点写 30–60 秒口播讲稿。"_ustr,
            u"讲稿|口播|备注讲稿|speaker notes|演讲稿|旁白"_ustr);
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

    // —— PDF / 材料 Skill packs（本地提取 · 不宣称 Acrobat）——
    v.push_back(makeBuiltin(
        u"pdf-summarize"_ustr, u"PDF 摘要"_ustr, u"general"_ustr, u"any"_ustr, u"summarize"_ustr,
        u"/PDF摘要"_ustr,
        u"【Skill · PDF/材料摘要 · 本地】仅依据已提取文本，不宣称完整 PDF 编辑。\n"
        u"## 何时使用\n"
        u"用户 @文件:…pdf 或要摘要/要点/风险清单。\n"
        u"## 步骤\n"
        u"1) 一句话主题；\n"
        u"2) 5–8 条要点（尽量带页/段线索）；\n"
        u"3) 风险/待核实；\n"
        u"4) 可落地下一步（打开编辑/转笔记/起草回函）。\n"
        u"## 硬规则\n"
        u"- 文本极少/扫描件：明确需 OCR，**不编造正文**；\n"
        u"- 禁止外网检索补全 PDF 内容；\n"
        u"- 不宣称 Acrobat 级编辑能力。\n"
        u"## 输出格式\n"
        u"### 主题\n…\n### 要点\n1)…\n### 风险\n…\n### 下一步\n…\n"_ustr
            + skillTrustFooter() + u"## 素材\n{selection}"_ustr,
        250));
    asSkill(v.back(),
            u"本地 PDF/材料摘要与要点，扫描件提示 OCR，不编造正文。"_ustr,
            u"PDF摘要|pdf摘要|摘要pdf|材料摘要|文件摘要|总结这个pdf|"
            u"pdf summary|@文件"_ustr);
    v.back().options.pinned = true;
    v.back().options.autoSubmit = false;
    v.push_back(makeBuiltin(
        u"pdf-qa"_ustr, u"PDF 问答"_ustr, u"general"_ustr, u"any"_ustr, u"chat"_ustr,
        u"/PDF问答"_ustr,
        u"【Skill · PDF/材料问答 · 本地】只根据已提取文本作答。\n"
        u"## 何时使用\n"
        u"针对 PDF/附件提问、核对条款、找某段是否出现。\n"
        u"## 步骤\n"
        u"1) 理解问题；\n"
        u"2) 在提取文本中定位；引用原文短句；\n"
        u"3) 找不到则明确「材料中未出现」，建议 @文件 或 OCR。\n"
        u"## 硬规则\n"
        u"- 禁止外网检索；不编造条款；\n"
        u"- 引用优先短句，勿大段抄袭式复述当证据。\n"
        u"## 输出格式\n"
        u"### 结论\n…\n### 依据（原文短句）\n…\n"_ustr
            + skillTrustFooter() + u"## 问题与素材\n{selection}"_ustr,
        275));
    asSkill(v.back(),
            u"基于本地提取文本的 PDF 问答，找不到就明说。"_ustr,
            u"PDF问答|pdf问答|问pdf|材料里有没有|合同条款|pdf qa|"
            u"这个文件说了"_ustr);
    v.push_back(makeBuiltin(
        u"pdf-to-outline"_ustr, u"PDF 转大纲"_ustr, u"general"_ustr, u"any"_ustr, u"plan"_ustr,
        u"/PDF转大纲"_ustr,
        u"【Skill · PDF→可编辑大纲】整理为 Writer 大纲或演示页序（默认不写回幻灯）。\n"
        u"## 何时使用\n"
        u"把 PDF/材料变成可编辑大纲、目录、演示页标题。\n"
        u"## 步骤\n"
        u"1) 提取结构；\n"
        u"2) 输出 H1/H2 条列（或页序标题）；\n"
        u"3) 关键数据保留；缺页【待补】。\n"
        u"## 硬规则\n"
        u"- 默认不用 ## 幻灯写回体，除非用户明确要写回演示；\n"
        u"- 不编造材料中没有的章节。\n"
        u"## 输出格式\n"
        u"# 标题\n## 节\n- 要点\n"_ustr
            + skillTrustFooter() + u"## 素材\n{selection}"_ustr,
        278));
    asSkill(v.back(),
            u"PDF/材料转 Writer 大纲或演示页序，保留关键数据。"_ustr,
            u"PDF转大纲|pdf大纲|转大纲|材料大纲|提取目录|pdf outline|"
            u"整理成大纲"_ustr);

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
        u"【Skill · 问本文档 · 本地】仅根据当前打开文档回答，禁止编造文档中不存在的内容；"
        u"引用要点时标明位置（段落/单元格/幻灯）；先结论后依据。\n"
        u"问题：\n{selection}"_ustr,
        290));
    asSkill(v.back(), u"只根据本文档回答，标明位置，不编造。"_ustr,
            u"问本文档|文档里|这篇说了|根据文档|ask document|文档问答|本文档"_ustr);
    v.back().options.pinned = true;
    v.back().options.includeDocContext = true;
    v.push_back(makeBuiltin(
        u"chart-assist"_ustr, u"图表助手"_ustr, u"calc"_ustr, u"calc"_ustr, u"plan"_ustr,
        u"/图表"_ustr,
        u"【Skill · 图表助手】推荐类型+系列+插入步骤；可选 = 辅助公式。\n"
        u"不编造业务数据。\n"_ustr
            + skillTrustFooter() + u"## 选区\n{selection}"_ustr,
        125));
    asSkill(v.back(), u"根据选区推荐图表类型与插入步骤。"_ustr,
            u"图表|做图|柱状图|折线图|饼图|chart|插入图表|可视化"_ustr);
    v.push_back(makeBuiltin(
        u"slide-page-edit"_ustr, u"本页改写"_ustr, u"impress"_ustr, u"impress"_ustr,
        u"rewrite"_ustr, u"/本页"_ustr,
        u"【Skill · 本页改写】只改当前页标题+要点，页数不变。\n"
        u"输出：第一行标题，其后每行要点；可附讲稿。\n"_ustr
            + skillTrustFooter() + u"## 当前页\n{selection}"_ustr,
        225));
    asSkill(v.back(), u"只改当前幻灯页文案，保持页数。"_ustr,
            u"本页改写|改这一页|当前页|本页文案|page edit"_ustr);
    v.push_back(makeBuiltin(
        u"multi-step-agent"_ustr, u"多步协作"_ustr, u"general"_ustr, u"any"_ustr, u"agent"_ustr,
        u"/多步"_ustr,
        u"【多步协作】规划→执行→审查完成下列目标（禁止暗改主文档）。\n目标：\n{selection}"_ustr,
        300, /*agent*/ true));
    asSkill(v.back(), u"多步规划→执行→审查，写回须批准。"_ustr,
            u"多步|多步协作|分步做|agent|plan-act|子代理|协作任务"_ustr);
    v.back().options.pinned = true;
    v.push_back(makeBuiltin(
        u"explain-selection"_ustr, u"解释选区"_ustr, u"general"_ustr, u"any"_ustr, u"chat"_ustr,
        u"/解释"_ustr,
        u"【Skill · 解释选区】含义、结构与可改进点；不写回。\n内容：\n{selection}"_ustr, 310));
    asSkill(v.back(), u"解释选区含义与可改进点。"_ustr,
            u"解释选区|解释一下|什么意思|explain selection|解读这段"_ustr);
    v.push_back(makeBuiltin(
        u"checklist-review"_ustr, u"清单审查"_ustr, u"general"_ustr, u"any"_ustr, u"review"_ustr,
        u"/清单审查"_ustr,
        u"【Skill · 清单审查】完整性/风险/表述：通过项与待改项。\n内容：\n{selection}"_ustr,
        320));
    asSkill(v.back(), u"检查清单式审查：通过项与待改项。"_ustr,
            u"清单审查|检查清单| completeness|checklist|风险清单|过一遍清单"_ustr);

    // —— 业务场景包 v1（飞书应用目录 → Office 模板工厂；非多维表运行时）——
    // 命名用「模板/台账/清单」，禁止「系统/平台」默认文案。requireApproval 已默认 true。
    // Skill v2: auto whenToUse from title + slash stem for NL match (本地模板，非 SaaS).
    auto attachBizSkill = [&](const OUString& title, const OUString& slash) {
        OUString stem = slash;
        if (stem.startsWith(u"/"_ustr))
            stem = stem.copy(1);
        OUStringBuffer triggers;
        triggers.append(title);
        triggers.append(u"|"_ustr);
        triggers.append(stem);
        triggers.append(u"|模板|台账|表单|本地模板"_ustr);
        asSkill(v.back(), title + u" · 本地模板（非在线业务系统）"_ustr,
                triggers.makeStringAndClear());
    };
    auto bizWriter = [&](const OUString& id, const OUString& title, const OUString& slash,
                         const OUString& prompt, sal_Int32 order) {
        v.push_back(makeBuiltin(id, title, u"writer"_ustr, u"writer"_ustr, u"plan"_ustr, slash,
                                prompt, order));
        v.back().options.autoSubmit = false;
        v.back().options.attachSelection = true;
        attachBizSkill(title, slash);
    };
    auto bizCalc = [&](const OUString& id, const OUString& title, const OUString& slash,
                       const OUString& prompt, sal_Int32 order) {
        v.push_back(makeBuiltin(id, title, u"calc"_ustr, u"calc"_ustr, u"plan"_ustr, slash, prompt,
                                order));
        v.back().options.autoSubmit = false;
        v.back().options.attachSelection = true;
        attachBizSkill(title, slash);
    };
    auto bizImpress = [&](const OUString& id, const OUString& title, const OUString& slash,
                          const OUString& prompt, sal_Int32 order) {
        v.push_back(makeBuiltin(id, title, u"impress"_ustr, u"impress"_ustr, u"plan"_ustr, slash,
                                prompt, order));
        v.back().options.autoSubmit = false;
        v.back().options.attachSelection = true;
        attachBizSkill(title, slash);
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
        appendJsonString(b, u"description", s.description);
        b.append(u",\n");
        appendJsonString(b, u"whenToUse", s.whenToUse);
        b.append(u",\n");
        appendJsonInt(b, u"skillVersion", s.skillVersion);
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
        s.description = jsonStringField(frag, u"description");
        s.whenToUse = jsonStringField(frag, u"whenToUse");
        s.skillVersion = jsonIntField(frag, u"skillVersion", 0);
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

/// Refresh factory skill packs when skillVersion lags (preserve pin/enabled/options).
void refreshBuiltinSkillPacks(ScenarioCatalog& cat)
{
    const auto builtins = DocumentAIScenarioStore::builtinDefaults();
    for (const auto& b : builtins)
    {
        if (b.skillVersion <= 0)
            continue;
        DocumentAIScenario* p = DocumentAIScenarioStore::findMutable(cat, b.id);
        if (!p || !p->builtin)
            continue;
        if (p->skillVersion >= b.skillVersion)
            continue;
        p->promptTemplate = b.promptTemplate;
        p->description = b.description;
        p->whenToUse = b.whenToUse;
        p->titleZh = b.titleZh;
        p->slashCommand = b.slashCommand;
        p->capabilityHint = b.capabilityHint;
        p->skillVersion = b.skillVersion;
    }
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
    {
        mergeMissingBuiltins(cat);
        refreshBuiltinSkillPacks(cat);
    }
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
        // Quality-core favorites (text / table / design / PDF)
        for (auto& s : cat.items)
        {
            if (s.id == u"official-polish"_ustr || s.id == u"layout-polish"_ustr
                || s.id == u"content-quality"_ustr || s.id == u"formula-assist"_ustr
                || s.id == u"data-clean"_ustr || s.id == u"table-format"_ustr
                || s.id == u"design-outline"_ustr || s.id == u"pdf-summarize"_ustr
                || s.id == u"ask-document"_ustr)
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

namespace
{
/// Put quality-core scenario ids first (stable among non-front items).
void promoteQualityFront(std::vector<DocumentAIScenario>& out, const OUString& want)
{
    std::vector<OUString> front;
    if (want == u"writer"_ustr)
        front = { u"layout-polish"_ustr, u"content-quality"_ustr, u"proofread"_ustr,
                  u"official-polish"_ustr };
    else if (want == u"calc"_ustr)
        front = { u"formula-assist"_ustr, u"data-clean"_ustr, u"table-format"_ustr,
                  u"data-summary"_ustr };
    else if (want == u"impress"_ustr)
        front = { u"design-outline"_ustr, u"design-variants"_ustr, u"design-apply"_ustr,
                  u"design-export"_ustr };
    else if (want == u"general"_ustr || want.isEmpty())
        front = { u"pdf-summarize"_ustr, u"pdf-qa"_ustr, u"ask-document"_ustr,
                  u"pdf-to-outline"_ustr };
    if (front.empty() || out.empty())
        return;
    std::vector<DocumentAIScenario> ordered;
    ordered.reserve(out.size());
    for (const auto& id : front)
    {
        for (const auto& s : out)
        {
            if (s.id == id)
            {
                ordered.push_back(s);
                break;
            }
        }
    }
    for (const auto& s : out)
    {
        bool already = false;
        for (const auto& id : front)
        {
            if (s.id == id)
            {
                already = true;
                break;
            }
        }
        if (!already)
            ordered.push_back(s);
    }
    out.swap(ordered);
}
} // namespace

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
    // Quality-core: fix first 4 grid slots for writing / table / design / PDF tabs.
    promoteQualityFront(out, want);
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
    // Always remove the queue file after take (do not leave an empty stub).
    // Matches shell ReadHomeFile(bRemove) and GUI smoke "file gone" checks.
    {
        OUString url;
        if (osl::FileBase::getFileURLFromSystemPath(path, url) == osl::FileBase::E_None)
            osl::File::remove(url);
        else if (!body.isEmpty())
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

OUString DocumentAIScenarioStore::expandSkillWithUtterance(const DocumentAIScenario& rScenario,
                                                           const OUString& rSelectionText,
                                                           const OUString& rUserUtterance)
{
    OUString base = expandPrompt(rScenario, rSelectionText);
    const OUString u = rUserUtterance.trim();
    if (u.isEmpty())
        return base;
    if (base.isEmpty())
        return u"用户原话："_ustr + u;
    return base + u"\n\n## 用户原话\n"_ustr + u;
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

namespace
{
/// Split whenToUse on | 、 , ； and line breaks.
void collectTriggers(const OUString& whenToUse, std::vector<OUString>& out)
{
    OUStringBuffer cur;
    auto flush = [&]() {
        const OUString t = cur.makeStringAndClear().trim();
        if (t.getLength() >= 2)
            out.push_back(t);
    };
    for (sal_Int32 i = 0; i < whenToUse.getLength(); ++i)
    {
        const sal_Unicode c = whenToUse[i];
        if (c == u'|' || c == u'、' || c == u',' || c == u';' || c == u'；' || c == u'\n'
            || c == u'\r')
            flush();
        else
            cur.append(c);
    }
    flush();
}

bool surfaceCompatible(const DocumentAIScenario& s, const OUString& filter)
{
    if (filter.isEmpty() || filter == u"any"_ustr || filter == u"all"_ustr)
        return true;
    const OUString pref = s.preferredSurface.toAsciiLowerCase().trim();
    const OUString cat = s.category.toAsciiLowerCase().trim();
    if (pref.isEmpty() || pref == u"any"_ustr || pref == filter)
        return true;
    if (cat == filter || cat == u"general"_ustr)
        return true;
    return false;
}

sal_Int32 scoreSkillMatch(const DocumentAIScenario& s, const OUString& userLow)
{
    if (!s.enabled)
        return 0;
    sal_Int32 score = 0;
    std::vector<OUString> triggers;
    collectTriggers(s.whenToUse, triggers);
    for (const auto& tr : triggers)
    {
        const OUString tlow = tr.toAsciiLowerCase();
        if (tlow.getLength() < 2)
            continue;
        if (userLow.indexOf(tlow) >= 0)
        {
            // Longer phrases score higher (more specific).
            score += 2 + std::min<sal_Int32>(4, tlow.getLength() / 2);
        }
    }
    if (!s.titleZh.isEmpty())
    {
        const OUString titleLow = s.titleZh.toAsciiLowerCase();
        if (titleLow.getLength() >= 2 && userLow.indexOf(titleLow) >= 0)
            score += 5;
    }
    if (!s.slashCommand.isEmpty())
    {
        OUString stem = s.slashCommand;
        if (stem.startsWith(u"/"_ustr))
            stem = stem.copy(1);
        const OUString stemLow = stem.toAsciiLowerCase();
        if (stemLow.getLength() >= 2 && userLow.indexOf(stemLow) >= 0)
            score += 4;
    }
    // Skill packs preferred over thin prompts when scores tie later.
    if (s.skillVersion > 0)
        score += 1;
    return score;
}
} // namespace

const DocumentAIScenario* DocumentAIScenarioStore::matchNaturalLanguage(
    const ScenarioCatalog& rCatalog, const OUString& rUserInput, const OUString& rSurfaceFilter,
    sal_Int32* pScoreOut)
{
    if (pScoreOut)
        *pScoreOut = 0;
    const OUString raw = rUserInput.trim();
    if (raw.isEmpty() || raw.getLength() < 2)
        return nullptr;
    // Slash path has its own matcher.
    if (raw.startsWith(u"/"_ustr))
        return nullptr;
    // Ultra-short continue tokens — do not steal session flow.
    const OUString low = raw.toAsciiLowerCase();
    if (low == u"继续"_ustr || low == u"continue"_ustr || low == u"ok"_ustr || low == u"好的"_ustr
        || low == u"嗯"_ustr)
        return nullptr;

    const OUString filter = rSurfaceFilter.toAsciiLowerCase().trim();
    const DocumentAIScenario* best = nullptr;
    sal_Int32 bestScore = 0;
    for (const auto& s : rCatalog.items)
    {
        if (!s.enabled)
            continue;
        if (s.whenToUse.isEmpty() && s.skillVersion <= 0)
            continue; // only skill-tagged or whenToUse-bearing items
        if (!surfaceCompatible(s, filter))
            continue;
        const sal_Int32 sc = scoreSkillMatch(s, low);
        if (sc > bestScore)
        {
            bestScore = sc;
            best = &s;
        }
    }
    // Threshold: need a real phrase hit (title alone is 5; single 2-char trigger ~3).
    constexpr sal_Int32 kMinScore = 4;
    if (!best || bestScore < kMinScore)
        return nullptr;
    if (pScoreOut)
        *pScoreOut = bestScore;
    return best;
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
