/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "DocumentAIRewriteMemory.hxx"

#include <osl/file.hxx>
#include <rtl/ustrbuf.hxx>
#include <rtl/string.hxx>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace kqoffice::ai::chat
{
namespace
{
OUString clip(const OUString& s, sal_Int32 n)
{
    if (s.getLength() <= n)
        return s;
    return s.copy(0, n) + u"…"_ustr;
}

OUString lower(const OUString& s) { return s.toAsciiLowerCase(); }

bool writeFileUtf8(const OUString& rSystemPath, const std::string& body)
{
    std::ofstream out(OUStringToOString(rSystemPath, RTL_TEXTENCODING_UTF8).getStr(),
                      std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << body;
    return static_cast<bool>(out);
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

void ensureParentDir(const OUString& path)
{
    const sal_Int32 slash = path.lastIndexOf(u'/');
    if (slash <= 0)
        return;
    const OUString dir = path.copy(0, slash);
    OUString dirUrl;
    if (osl::FileBase::getFileURLFromSystemPath(dir, dirUrl) == osl::FileBase::E_None)
        osl::Directory::createPath(dirUrl);
}

void pushUnique(std::vector<OUString>& vec, const OUString& item, sal_Int32 maxN)
{
    const OUString t = item.trim();
    if (t.getLength() < 2)
        return;
    const OUString clipped = clip(t, 120);
    for (const auto& e : vec)
    {
        if (e == clipped)
            return;
        // Prefer longer superseding shorter
        if (e.indexOf(clipped) >= 0)
            return;
        if (clipped.indexOf(e) >= 0)
        {
            // replace shorter with longer later
        }
    }
    // Remove entries fully contained in new
    std::vector<OUString> next;
    next.reserve(vec.size() + 1);
    for (const auto& e : vec)
    {
        if (clipped.indexOf(e) >= 0 && e != clipped)
            continue; // drop shorter
        next.push_back(e);
    }
    next.push_back(clipped);
    while (static_cast<sal_Int32>(next.size()) > maxN)
        next.erase(next.begin());
    vec.swap(next);
}

bool looksLikeConstraintClause(const OUString& clause)
{
    const OUString low = lower(clause);
    return low.indexOf(u"别动"_ustr) >= 0 || low.indexOf(u"不要动"_ustr) >= 0
           || low.indexOf(u"勿改"_ustr) >= 0 || low.indexOf(u"不要改"_ustr) >= 0
           || low.indexOf(u"别改"_ustr) >= 0 || low.indexOf(u"禁止"_ustr) >= 0
           || low.indexOf(u"不可"_ustr) >= 0 || low.indexOf(u"不能改"_ustr) >= 0
           || low.indexOf(u"保持"_ustr) >= 0 || low.indexOf(u"保留"_ustr) >= 0
           || low.indexOf(u"沿用"_ustr) >= 0 || low.indexOf(u"务必不要"_ustr) >= 0
           || low.indexOf(u"不要碰"_ustr) >= 0 || low.indexOf(u"别碰"_ustr) >= 0
           || low.indexOf(u"don't change"_ustr) >= 0 || low.indexOf(u"do not change"_ustr) >= 0
           || low.indexOf(u"keep "_ustr) >= 0 || low.indexOf(u"preserve"_ustr) >= 0
           || low.indexOf(u"不得"_ustr) >= 0 || low.indexOf(u"严禁"_ustr) >= 0;
}

bool looksLikeCorrection(const OUString& clause)
{
    const OUString low = lower(clause);
    return low.indexOf(u"我说的是"_ustr) >= 0 || low.indexOf(u"改错了"_ustr) >= 0
           || low.indexOf(u"不是这个意思"_ustr) >= 0 || low.indexOf(u"你理解错了"_ustr) >= 0
           || low.indexOf(u"不要改成"_ustr) >= 0 || low.indexOf(u"我说错了"_ustr) >= 0
           || low.indexOf(u"纠正"_ustr) >= 0 || low.indexOf(u"更正"_ustr) >= 0
           || low.indexOf(u"actually"_ustr) >= 0 || low.indexOf(u"i meant"_ustr) >= 0;
}

void splitClauses(const OUString& text, std::vector<OUString>& out)
{
    OUStringBuffer cur;
    auto flush = [&]() {
        const OUString t = cur.makeStringAndClear().trim();
        if (t.getLength() >= 2)
            out.push_back(t);
    };
    for (sal_Int32 i = 0; i < text.getLength(); ++i)
    {
        const sal_Unicode c = text[i];
        if (c == u'，' || c == u',' || c == u'。' || c == u';' || c == u'；' || c == u'\n'
            || c == u'\r' || c == u'!' || c == u'！' || c == u'?' || c == u'？')
            flush();
        else
            cur.append(c);
    }
    flush();
}

OUString fieldLine(const OUString& body, const OUString& key)
{
    const OUString needle = key + u":"_ustr;
    sal_Int32 pos = 0;
    while (pos < body.getLength())
    {
        sal_Int32 nl = body.indexOf(u'\n', pos);
        if (nl < 0)
            nl = body.getLength();
        OUString line = body.copy(pos, nl - pos).trim();
        if (line.startsWith(needle))
            return line.copy(needle.getLength()).trim();
        pos = nl + 1;
    }
    return OUString();
}

void collectPrefixed(const OUString& body, const OUString& key, std::vector<OUString>& out)
{
    const OUString needle = key + u":"_ustr;
    sal_Int32 pos = 0;
    while (pos < body.getLength())
    {
        sal_Int32 nl = body.indexOf(u'\n', pos);
        if (nl < 0)
            nl = body.getLength();
        OUString line = body.copy(pos, nl - pos).trim();
        if (line.startsWith(needle))
        {
            const OUString v = line.copy(needle.getLength()).trim();
            if (!v.isEmpty())
                out.push_back(v);
        }
        pos = nl + 1;
    }
}
} // namespace

OUString DocumentAIRewriteMemory::storageRootPath()
{
    const char* env = std::getenv("KQOFFICE_AI_REWRITE_MEMORY");
    if (env && *env)
        return OUString::fromUtf8(env);
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return OUString();
    return OUString::fromUtf8(home) + u"/.config/kqoffice/ai-rewrite-memory"_ustr;
}

OUString DocumentAIRewriteMemory::cardPathForKey(const OUString& rDocumentKey)
{
    const OUString root = storageRootPath();
    if (root.isEmpty() || rDocumentKey.isEmpty())
        return OUString();
    // Sanitize key to filename-safe (hash is hex already).
    OUString key = rDocumentKey;
    for (sal_Int32 i = 0; i < key.getLength(); ++i)
    {
        const sal_Unicode c = key[i];
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F')
              || c == u'-' || c == u'_'))
            return root + u"/unknown.memory"_ustr;
    }
    return root + u"/"_ustr + key + u".memory"_ustr;
}

RewriteMemoryCard DocumentAIRewriteMemory::load(const OUString& rDocumentKey)
{
    RewriteMemoryCard c;
    c.documentKey = rDocumentKey;
    if (rDocumentKey.isEmpty())
        return c;
    const OUString path = cardPathForKey(rDocumentKey);
    if (path.isEmpty())
        return c;
    const OUString body = readFileUtf8(path);
    if (body.isEmpty())
        return c;

    c.summaryZh = fieldLine(body, u"summary"_ustr);
    // multi-line summary: if starts with | take until blank constraint block — keep simple single line
    c.lastSkillId = fieldLine(body, u"lastSkillId"_ustr);
    c.lastSkillTitle = fieldLine(body, u"lastSkillTitle"_ustr);
    c.lastObjective = fieldLine(body, u"lastObjective"_ustr);
    c.brandTone = fieldLine(body, u"brandTone"_ustr);
    const OUString tc = fieldLine(body, u"turnCount"_ustr);
    if (!tc.isEmpty())
        c.turnCount = tc.toInt32();
    const OUString cg = fieldLine(body, u"compactGeneration"_ustr);
    if (!cg.isEmpty())
        c.compactGeneration = cg.toInt32();
    collectPrefixed(body, u"constraint"_ustr, c.constraints);
    collectPrefixed(body, u"correction"_ustr, c.corrections);
    return c;
}

bool DocumentAIRewriteMemory::save(const RewriteMemoryCard& rCard)
{
    if (rCard.documentKey.isEmpty())
        return false;
    const OUString path = cardPathForKey(rCard.documentKey);
    if (path.isEmpty())
        return false;
    ensureParentDir(path);
    OUStringBuffer b;
    b.append(u"schema: v1-rewrite-memory\n"_ustr);
    b.append(u"documentKey:"_ustr);
    b.append(rCard.documentKey);
    b.append(u"\n"_ustr);
    b.append(u"turnCount:"_ustr);
    b.append(OUString::number(rCard.turnCount));
    b.append(u"\n"_ustr);
    b.append(u"compactGeneration:"_ustr);
    b.append(OUString::number(rCard.compactGeneration));
    b.append(u"\n"_ustr);
    b.append(u"lastSkillId:"_ustr);
    b.append(rCard.lastSkillId);
    b.append(u"\n"_ustr);
    b.append(u"lastSkillTitle:"_ustr);
    b.append(rCard.lastSkillTitle);
    b.append(u"\n"_ustr);
    b.append(u"lastObjective:"_ustr);
    b.append(clip(rCard.lastObjective, 200));
    b.append(u"\n"_ustr);
    b.append(u"brandTone:"_ustr);
    b.append(clip(rCard.brandTone, 80));
    b.append(u"\n"_ustr);
    b.append(u"summary:"_ustr);
    b.append(clip(rCard.summaryZh.replaceAll(u"\n"_ustr, u" "_ustr), 800));
    b.append(u"\n"_ustr);
    for (const auto& x : rCard.constraints)
    {
        b.append(u"constraint:"_ustr);
        b.append(x.replaceAll(u"\n"_ustr, u" "_ustr));
        b.append(u"\n"_ustr);
    }
    for (const auto& x : rCard.corrections)
    {
        b.append(u"correction:"_ustr);
        b.append(x.replaceAll(u"\n"_ustr, u" "_ustr));
        b.append(u"\n"_ustr);
    }
    return writeFileUtf8(path, std::string(OUStringToOString(b.makeStringAndClear(),
                                                             RTL_TEXTENCODING_UTF8)));
}

bool DocumentAIRewriteMemory::clear(const OUString& rDocumentKey)
{
    const OUString path = cardPathForKey(rDocumentKey);
    if (path.isEmpty())
        return false;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return false;
    const auto rc = osl::File::remove(url);
    return rc == osl::FileBase::E_None || rc == osl::FileBase::E_NOENT;
}

void DocumentAIRewriteMemory::addConstraint(RewriteMemoryCard& rCard, const OUString& rConstraint)
{
    pushUnique(rCard.constraints, rConstraint, kMaxConstraints);
    rCard.dirty = true;
}

void DocumentAIRewriteMemory::ingestUserTurn(RewriteMemoryCard& rCard, const OUString& rUserPrompt)
{
    const OUString t = rUserPrompt.trim();
    if (t.isEmpty())
        return;
    ++rCard.turnCount;
    rCard.dirty = true;

    // Explicit /记住 handled by panel via extractRememberFact; still scan freeform.
    std::vector<OUString> clauses;
    splitClauses(t, clauses);
    for (const auto& cl : clauses)
    {
        if (looksLikeConstraintClause(cl))
            pushUnique(rCard.constraints, cl, kMaxConstraints);
        if (looksLikeCorrection(cl))
            pushUnique(rCard.corrections, cl, kMaxCorrections);
    }

    // Tone shortcuts
    const OUString low = lower(t);
    if (rCard.brandTone.isEmpty()
        && (low.indexOf(u"正式"_ustr) >= 0 || low.indexOf(u"庄重"_ustr) >= 0))
        rCard.brandTone = u"正式/庄重"_ustr;
    if (low.indexOf(u"口语"_ustr) >= 0 || low.indexOf(u"轻松"_ustr) >= 0)
        rCard.brandTone = u"口语/轻松"_ustr;
    if (low.indexOf(u"简洁"_ustr) >= 0 || low.indexOf(u"精简"_ustr) >= 0)
        pushUnique(rCard.constraints, u"表述简洁，删繁就简"_ustr, kMaxConstraints);
}

void DocumentAIRewriteMemory::noteSkill(RewriteMemoryCard& rCard, const OUString& rSkillId,
                                        const OUString& rSkillTitle)
{
    if (!rSkillId.isEmpty())
    {
        rCard.lastSkillId = rSkillId;
        rCard.dirty = true;
    }
    if (!rSkillTitle.isEmpty())
    {
        rCard.lastSkillTitle = rSkillTitle;
        rCard.dirty = true;
    }
}

void DocumentAIRewriteMemory::noteObjective(RewriteMemoryCard& rCard, const OUString& rObjective)
{
    if (rObjective.isEmpty())
        return;
    rCard.lastObjective = clip(rObjective, 200);
    rCard.dirty = true;
}

void DocumentAIRewriteMemory::ingestWorkPlanNotes(RewriteMemoryCard& rCard,
                                                  const OUString& rScopeOut,
                                                  const OUString& rReviseNotes,
                                                  const OUString& rObjective)
{
    if (!rObjective.isEmpty())
        noteObjective(rCard, rObjective);
    if (!rScopeOut.isEmpty())
    {
        std::vector<OUString> clauses;
        splitClauses(rScopeOut, clauses);
        for (const auto& cl : clauses)
            pushUnique(rCard.constraints, cl, kMaxConstraints);
    }
    if (!rReviseNotes.isEmpty())
        pushUnique(rCard.corrections, rReviseNotes, kMaxCorrections);
    rCard.dirty = true;
}

void DocumentAIRewriteMemory::compact(RewriteMemoryCard& rCard, const OUString& rRecentTurnsText)
{
    OUStringBuffer s;
    s.append(u"本会话改稿记忆（本地）："_ustr);
    if (!rCard.lastObjective.isEmpty())
    {
        s.append(u" 目标="_ustr);
        s.append(clip(rCard.lastObjective, 80));
        s.append(u"。"_ustr);
    }
    if (!rCard.lastSkillTitle.isEmpty() || !rCard.lastSkillId.isEmpty())
    {
        s.append(u" 技能="_ustr);
        s.append(rCard.lastSkillTitle.isEmpty() ? rCard.lastSkillId : rCard.lastSkillTitle);
        s.append(u"。"_ustr);
    }
    if (!rCard.brandTone.isEmpty())
    {
        s.append(u" 语气="_ustr);
        s.append(rCard.brandTone);
        s.append(u"。"_ustr);
    }
    if (!rCard.constraints.empty())
    {
        s.append(u" 硬约束："_ustr);
        for (size_t i = 0; i < rCard.constraints.size(); ++i)
        {
            if (i)
                s.append(u"；"_ustr);
            s.append(rCard.constraints[i]);
        }
        s.append(u"。"_ustr);
    }
    if (!rCard.corrections.empty())
    {
        s.append(u" 用户纠偏："_ustr);
        for (size_t i = 0; i < rCard.corrections.size(); ++i)
        {
            if (i)
                s.append(u"；"_ustr);
            s.append(clip(rCard.corrections[i], 60));
        }
        s.append(u"。"_ustr);
    }
    if (!rRecentTurnsText.isEmpty())
    {
        s.append(u" 近况摘要线索："_ustr);
        s.append(clip(rRecentTurnsText.replaceAll(u"\n"_ustr, u" | "_ustr), 200));
        s.append(u"。"_ustr);
    }
    s.append(u" 回合="_ustr);
    s.append(OUString::number(rCard.turnCount));
    s.append(u"。未批准不改主文档。"_ustr);
    rCard.summaryZh = s.makeStringAndClear();
    ++rCard.compactGeneration;
    rCard.dirty = true;
}

void DocumentAIRewriteMemory::afterAssistantTurn(RewriteMemoryCard& rCard,
                                                 const OUString& rAssistantSnippet,
                                                 const OUString& rRecentTurnsText)
{
    (void)rAssistantSnippet;
    if (rCard.turnCount <= 0)
        return;
    if (rCard.turnCount % compactEveryNTurns() == 0 || rCard.summaryZh.isEmpty())
        compact(rCard, rRecentTurnsText);
}

OUString DocumentAIRewriteMemory::formatPromptBlock(const RewriteMemoryCard& rCard)
{
    if (rCard.constraints.empty() && rCard.corrections.empty() && rCard.summaryZh.isEmpty()
        && rCard.lastObjective.isEmpty() && rCard.brandTone.isEmpty())
        return OUString();

    OUStringBuffer b;
    b.append(u"【同文档改稿记忆 — 必须遵守；优先于一般润色偏好】\n"_ustr);
    if (!rCard.summaryZh.isEmpty())
    {
        b.append(u"摘要："_ustr);
        b.append(rCard.summaryZh);
        b.append(u"\n"_ustr);
    }
    if (!rCard.lastObjective.isEmpty())
    {
        b.append(u"当前目标："_ustr);
        b.append(rCard.lastObjective);
        b.append(u"\n"_ustr);
    }
    if (!rCard.brandTone.isEmpty())
    {
        b.append(u"语气："_ustr);
        b.append(rCard.brandTone);
        b.append(u"\n"_ustr);
    }
    if (!rCard.constraints.empty())
    {
        b.append(u"硬约束：\n"_ustr);
        for (const auto& c : rCard.constraints)
        {
            b.append(u"- "_ustr);
            b.append(c);
            b.append(u"\n"_ustr);
        }
    }
    if (!rCard.corrections.empty())
    {
        b.append(u"用户纠偏（方向变更，勿回退）：\n"_ustr);
        for (const auto& c : rCard.corrections)
        {
            b.append(u"- "_ustr);
            b.append(c);
            b.append(u"\n"_ustr);
        }
    }
    b.append(u"（记忆仅本地；写回仍须用户批准。）\n"_ustr);
    return b.makeStringAndClear();
}

OUString DocumentAIRewriteMemory::formatUserVisible(const RewriteMemoryCard& rCard)
{
    OUStringBuffer b;
    b.append(u"# 改稿记忆（本地 · 本文档）\n\n"_ustr);
    b.append(u"- 文档键：`"_ustr);
    b.append(clip(rCard.documentKey, 16));
    b.append(u"…`\n"_ustr);
    b.append(u"- 用户回合："_ustr);
    b.append(OUString::number(rCard.turnCount));
    b.append(u" · 压缩代数："_ustr);
    b.append(OUString::number(rCard.compactGeneration));
    b.append(u"\n"_ustr);
    if (!rCard.lastSkillTitle.isEmpty() || !rCard.lastSkillId.isEmpty())
    {
        b.append(u"- 最近技能："_ustr);
        b.append(rCard.lastSkillTitle.isEmpty() ? rCard.lastSkillId : rCard.lastSkillTitle);
        b.append(u"\n"_ustr);
    }
    if (!rCard.lastObjective.isEmpty())
    {
        b.append(u"- 目标："_ustr);
        b.append(rCard.lastObjective);
        b.append(u"\n"_ustr);
    }
    if (!rCard.brandTone.isEmpty())
    {
        b.append(u"- 语气："_ustr);
        b.append(rCard.brandTone);
        b.append(u"\n"_ustr);
    }
    b.append(u"\n## 硬约束\n"_ustr);
    if (rCard.constraints.empty())
        b.append(u"（无）\n"_ustr);
    else
        for (const auto& c : rCard.constraints)
        {
            b.append(u"- "_ustr);
            b.append(c);
            b.append(u"\n"_ustr);
        }
    b.append(u"\n## 用户纠偏\n"_ustr);
    if (rCard.corrections.empty())
        b.append(u"（无）\n"_ustr);
    else
        for (const auto& c : rCard.corrections)
        {
            b.append(u"- "_ustr);
            b.append(c);
            b.append(u"\n"_ustr);
        }
    b.append(u"\n## 摘要\n"_ustr);
    b.append(rCard.summaryZh.isEmpty() ? u"（尚未压缩；多轮后自动生成）\n"_ustr : rCard.summaryZh);
    b.append(u"\n\n---\n"_ustr);
    b.append(u"`/记住 别动金额列` · `/忘记 金额` · `/忘记全部` · `/compact` 强制压缩\n"_ustr);
    return b.makeStringAndClear();
}

RewriteMemoryAction DocumentAIRewriteMemory::classifyAction(const OUString& rPrompt)
{
    const OUString t = rPrompt.trim();
    if (t.isEmpty())
        return RewriteMemoryAction::None;
    const OUString low = lower(t);
    if (t.startsWith(u"/memory"_ustr) || t.startsWith(u"/改稿记忆"_ustr)
        || low == u"改稿记忆"_ustr || low == u"查看记忆"_ustr)
        return RewriteMemoryAction::Show;
    if (t.startsWith(u"/compact"_ustr) || t.startsWith(u"/压缩记忆"_ustr)
        || low == u"压缩记忆"_ustr)
        return RewriteMemoryAction::Compact;
    if (t.startsWith(u"/记住"_ustr) || t.startsWith(u"/remember"_ustr)
        || t.startsWith(u"记住："_ustr) || t.startsWith(u"记住:"_ustr))
        return RewriteMemoryAction::Remember;
    if (t.startsWith(u"/忘记"_ustr) || t.startsWith(u"/forget"_ustr)
        || t.startsWith(u"忘记："_ustr) || t.startsWith(u"忘记:"_ustr)
        || low == u"忘记全部"_ustr || low == u"清空记忆"_ustr)
        return RewriteMemoryAction::Forget;
    return RewriteMemoryAction::None;
}

OUString DocumentAIRewriteMemory::extractRememberFact(const OUString& rPrompt)
{
    const OUString t = rPrompt.trim();
    auto after = [&](const OUString& key) -> OUString {
        if (t.startsWith(key))
            return t.copy(key.getLength()).trim();
        return OUString();
    };
    OUString n = after(u"/记住"_ustr);
    if (n.isEmpty())
        n = after(u"/remember"_ustr);
    if (n.isEmpty())
        n = after(u"记住："_ustr);
    if (n.isEmpty())
        n = after(u"记住:"_ustr);
    if (n.isEmpty())
        n = after(u"记住 "_ustr);
    while (!n.isEmpty() && (n[0] == u':' || n[0] == u'：' || n[0] == u' '))
        n = n.copy(1).trim();
    return n;
}

OUString DocumentAIRewriteMemory::extractForgetTarget(const OUString& rPrompt)
{
    const OUString t = rPrompt.trim();
    const OUString low = lower(t);
    if (low == u"忘记全部"_ustr || low == u"清空记忆"_ustr || low == u"/忘记全部"_ustr
        || low == u"/forget all"_ustr || low == u"/forgetall"_ustr)
        return u"*"_ustr;
    auto after = [&](const OUString& key) -> OUString {
        if (t.startsWith(key))
            return t.copy(key.getLength()).trim();
        return OUString();
    };
    OUString n = after(u"/忘记"_ustr);
    if (n.isEmpty())
        n = after(u"/forget"_ustr);
    if (n.isEmpty())
        n = after(u"忘记："_ustr);
    if (n.isEmpty())
        n = after(u"忘记:"_ustr);
    if (n.isEmpty())
        n = after(u"忘记 "_ustr);
    while (!n.isEmpty() && (n[0] == u':' || n[0] == u'：' || n[0] == u' '))
        n = n.copy(1).trim();
    if (lower(n) == u"全部"_ustr || lower(n) == u"all"_ustr || lower(n) == u"*"_ustr)
        return u"*"_ustr;
    return n;
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
