/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "DocumentAIVisionEvidence.hxx"

#include <ModelRoles.hxx>
#include <ModelRoutingConfig.hxx>
#include <OllamaAdapter.hxx>

#include <osl/file.hxx>
#include <osl/process.h>
#include <osl/time.h>
#include <rtl/strbuf.hxx>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#if !defined(_WIN32)
#include <unistd.h>
#endif
#include <vector>

namespace kqoffice::ai::chat
{
namespace
{
constexpr sal_Int64 kMaxVisionFileBytes = 2 * 1024 * 1024; // 2 MiB per PNG

OUString clip(const OUString& s, sal_Int32 n)
{
    if (s.getLength() <= n)
        return s;
    return s.copy(0, n) + u"…"_ustr;
}

OUString envOrEmpty(const char* name)
{
    const char* v = std::getenv(name);
    if (!v || !*v)
        return OUString();
    return OUString::fromUtf8(v);
}

std::string base64Encode(const std::vector<char>& data)
{
    static const char* kTbl
        = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    const auto* p = reinterpret_cast<const unsigned char*>(data.data());
    const std::size_t n = data.size();
    std::size_t i = 0;
    while (i + 2 < n)
    {
        const unsigned int v = (static_cast<unsigned int>(p[i]) << 16)
                               | (static_cast<unsigned int>(p[i + 1]) << 8)
                               | static_cast<unsigned int>(p[i + 2]);
        out.push_back(kTbl[(v >> 18) & 63]);
        out.push_back(kTbl[(v >> 12) & 63]);
        out.push_back(kTbl[(v >> 6) & 63]);
        out.push_back(kTbl[v & 63]);
        i += 3;
    }
    if (i < n)
    {
        unsigned int v = static_cast<unsigned int>(p[i]) << 16;
        if (i + 1 < n)
            v |= static_cast<unsigned int>(p[i + 1]) << 8;
        out.push_back(kTbl[(v >> 18) & 63]);
        out.push_back(kTbl[(v >> 12) & 63]);
        out.push_back(i + 1 < n ? kTbl[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

bool readFileBytes(const OUString& rSysPath, std::vector<char>& out, sal_Int64 maxBytes)
{
    out.clear();
    if (rSysPath.isEmpty())
        return false;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0 || static_cast<sal_Int64>(sz) > maxBytes)
    {
        f.close();
        return false;
    }
    out.resize(static_cast<std::size_t>(sz));
    sal_uInt64 n = 0;
    const bool ok = f.read(out.data(), sz, n) == osl::FileBase::E_None && n == sz;
    f.close();
    if (!ok)
        out.clear();
    return ok;
}

OUString jsonEscape(const OUString& s)
{
    OUStringBuffer b;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        if (c == u'\\' || c == u'"')
            b.append(u'\\');
        if (c == u'\n')
        {
            b.append(u"\\n"_ustr);
            continue;
        }
        if (c == u'\r')
            continue;
        b.append(c);
    }
    return b.makeStringAndClear();
}

OUString extractJsonStringField(const std::string& body, const char* key)
{
    const std::string needle = std::string("\"") + key + "\"";
    auto p = body.find(needle);
    if (p == std::string::npos)
        return OUString();
    p = body.find(':', p + needle.size());
    if (p == std::string::npos)
        return OUString();
    p = body.find('"', p + 1);
    if (p == std::string::npos)
        return OUString();
    ++p;
    std::string val;
    while (p < body.size())
    {
        if (body[p] == '\\' && p + 1 < body.size())
        {
            const char e = body[p + 1];
            if (e == 'n')
                val.push_back('\n');
            else if (e == 't')
                val.push_back('\t');
            else
                val.push_back(e);
            p += 2;
            continue;
        }
        if (body[p] == '"')
            break;
        val.push_back(body[p]);
        ++p;
    }
    return OUString::fromUtf8(std::string_view(val.data(), val.size()));
}

/// curl POST to Ollama loopback only. Images never leave the machine.
OUString ollamaChatLocalVision(const OUString& model, const OUString& prompt,
                               const std::vector<std::string>& imagesB64)
{
    if (model.isEmpty() || imagesB64.empty())
        return OUString();

    // Build JSON body in temp file (images can be large).
    char tmpl[] = "/tmp/kqoffice-vision-XXXXXX";
    const int fd = ::mkstemp(tmpl);
    if (fd < 0)
        return OUString();
    ::close(fd);

    {
        OUStringBuffer jb;
        jb.append(u"{\"model\":\""_ustr);
        jb.append(jsonEscape(model));
        jb.append(u"\",\"stream\":false,\"messages\":[{\"role\":\"user\",\"content\":\""_ustr);
        jb.append(jsonEscape(prompt));
        jb.append(u"\",\"images\":["_ustr);
        for (std::size_t i = 0; i < imagesB64.size(); ++i)
        {
            if (i)
                jb.append(u',');
            jb.append(u'"');
            jb.append(OUString::fromUtf8(std::string_view(imagesB64[i].data(), imagesB64[i].size())));
            jb.append(u'"');
        }
        jb.append(u"]}]}"_ustr);
        const OString utf8 = OUStringToOString(jb.makeStringAndClear(), RTL_TEXTENCODING_UTF8);
        FILE* wf = std::fopen(tmpl, "wb");
        if (!wf)
        {
            ::unlink(tmpl);
            return OUString();
        }
        std::fwrite(utf8.getStr(), 1, static_cast<std::size_t>(utf8.getLength()), wf);
        std::fclose(wf);
    }

    // Hard-lock destination to loopback Ollama.
    OString cmd = "curl -sS --http1.1 --max-time 45 --connect-timeout 2 "
                  "-H 'Content-Type: application/json' "
                  "--data-binary @"_ostr
                  + OString(tmpl)
                  + " http://127.0.0.1:11434/api/chat 2>/dev/null"_ostr;
    FILE* pipe = ::popen(cmd.getStr(), "r");
    if (!pipe)
    {
        ::unlink(tmpl);
        return OUString();
    }
    std::string resp;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe))
        resp.append(buf);
    ::pclose(pipe);
    ::unlink(tmpl);

    // Prefer message.content (chat API); fall back to response (generate-style).
    OUString content = extractJsonStringField(resp, "content");
    if (content.isEmpty())
        content = extractJsonStringField(resp, "response");
    // content may nest under message — if we got a giant blob with multiple "content",
    // take last non-empty reasonable length (already linear extract first).
    if (content.getLength() > 8000)
        content = content.copy(0, 8000) + u"…"_ustr;
    return content.trim();
}

OUString runVisionCmd(const OUString& tmpl, const OUString& pre, const OUString& post,
                      const OUString& prompt)
{
    if (tmpl.isEmpty())
        return OUString();
    OUString cmd = tmpl;
    cmd = cmd.replaceAll(u"$PRE"_ustr, pre);
    cmd = cmd.replaceAll(u"$POST"_ustr, post);
    cmd = cmd.replaceAll(u"$PROMPT"_ustr, u"\"visual-audit\""_ustr);
    (void)prompt; // external tools typically read images only
    const OString c = OUStringToOString(cmd, RTL_TEXTENCODING_UTF8);
    FILE* pipe = ::popen(c.getStr(), "r");
    if (!pipe)
        return OUString();
    std::string out;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe) && out.size() < 16000)
        out.append(buf);
    ::pclose(pipe);
    return OUString::fromUtf8(std::string_view(out.data(), out.size())).trim();
}
} // namespace

VisionFileMeta DocumentAIVisionEvidence::statLocalFile(const OUString& rSysPath)
{
    VisionFileMeta m;
    m.path = rSysPath;
    if (rSysPath.isEmpty())
        return m;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return m;
    osl::DirectoryItem item;
    if (osl::DirectoryItem::get(url, item) != osl::FileBase::E_None)
        return m;
    osl::FileStatus st(osl_FileStatus_Mask_FileSize | osl_FileStatus_Mask_ModifyTime
                       | osl_FileStatus_Mask_Type);
    if (item.getFileStatus(st) != osl::FileBase::E_None)
        return m;
    if (st.getFileType() == osl::FileStatus::Directory)
        return m;
    m.exists = true;
    m.sizeBytes = static_cast<sal_Int64>(st.getFileSize());
    TimeValue tv = st.getModifyTime();
    m.mtimeSec = static_cast<sal_Int64>(tv.Seconds);
    return m;
}

OUString DocumentAIVisionEvidence::softFingerprint(const OUString& rSysPath)
{
    if (rSysPath.isEmpty())
        return OUString();
    std::vector<char> bytes;
    if (!readFileBytes(rSysPath, bytes, 4096))
    {
        // still allow size-only fingerprint
        const VisionFileMeta m = statLocalFile(rSysPath);
        if (!m.exists)
            return OUString();
        return u"sz="_ustr + OUString::number(m.sizeBytes) + u"|empty-sample"_ustr;
    }
    // FNV-1a 64-bit style over sample
    sal_uInt64 h = 14695981039346656037ull;
    for (unsigned char c : bytes)
    {
        h ^= static_cast<sal_uInt64>(c);
        h *= 1099511628211ull;
    }
    const VisionFileMeta m = statLocalFile(rSysPath);
    OUStringBuffer b;
    b.append(u"sz="_ustr);
    b.append(OUString::number(m.sizeBytes));
    b.append(u"|h="_ustr);
    // hex of low 32 bits is enough for soft compare
    const sal_uInt32 lo = static_cast<sal_uInt32>(h & 0xffffffffu);
    static const char* hex = "0123456789abcdef";
    for (int i = 7; i >= 0; --i)
        b.append(sal_Unicode(hex[(lo >> (i * 4)) & 0xf]));
    b.append(u"|n="_ustr);
    b.append(OUString::number(static_cast<sal_Int32>(bytes.size())));
    return b.makeStringAndClear();
}

OUString DocumentAIVisionEvidence::buildDiffCard(const VisionEvidenceReport& r,
                                                 const ApplyPlan& rPlan,
                                                 const DocumentAIApplyResult& rApply)
{
    OUStringBuffer d;
    d.append(u"【Vision 对比卡 · 本地 pre/post · 不上传】\n"_ustr);
    d.append(u"状态："_ustr);
    d.append(r.status);
    d.append(u" · "_ustr);
    d.append(r.summaryZh);
    d.append(u"\n"_ustr);
    if (!r.resolvedVisionModel.isEmpty())
    {
        d.append(u"视觉模型（解析）："_ustr);
        d.append(r.resolvedVisionModel);
        d.append(u"\n"_ustr);
    }
    d.append(u"体积 Δ="_ustr);
    d.append(OUString::number(r.sizeDeltaBytes));
    d.append(u" B · mtime Δ="_ustr);
    d.append(OUString::number(r.mtimeDeltaSec));
    d.append(u" s\n"_ustr);
    if (!r.preFingerprint.isEmpty() || !r.postFingerprint.isEmpty())
    {
        d.append(u"指纹 pre： "_ustr);
        d.append(r.preFingerprint.isEmpty() ? u"（无）"_ustr : r.preFingerprint);
        d.append(u"\n指纹 post："_ustr);
        d.append(r.postFingerprint.isEmpty() ? u"（无）"_ustr : r.postFingerprint);
        d.append(u"\n指纹一致："_ustr);
        d.append(r.fingerprintsMatch ? u"是（像素样本可能未变）"_ustr : u"否（有变化或仅一侧）"_ustr);
        d.append(u"\n"_ustr);
    }
    d.append(u"核对清单：\n"_ustr);
    if (!r.hasPre)
        d.append(u"☐ pre 截图缺失 — 重新批准写回以采集\n"_ustr);
    else
        d.append(u"☑ pre 存在 · "_ustr + clip(r.pre.path, 64) + u"\n"_ustr);
    if (!r.hasPost)
        d.append(u"☐ post 截图缺失 — 检查截屏权限\n"_ustr);
    else
        d.append(u"☑ post 存在 · "_ustr + clip(r.post.path, 64) + u"\n"_ustr);
    if (r.hasPre && r.hasPost && r.fingerprintsMatch && rApply.appliedCount > 0)
        d.append(u"⚠ 指纹一致但 applied>0 — 打开 pre/post 人工确认是否真有可见变化\n"_ustr);
    if (r.hasPre && r.hasPost && r.sizeDeltaBytes == 0 && rApply.appliedCount > 0)
        d.append(u"⚠ 体积未变但已写回 — 可能改动在屏外/同尺寸渲染\n"_ustr);

    sal_Int32 n = 0;
    for (const auto& op : rPlan.operations)
    {
        if (++n > 8)
            break;
        d.append(u"☐ 操作可见？ "_ustr);
        d.append(op.opType);
        if (!op.target.isEmpty())
        {
            d.append(u" @"_ustr);
            d.append(op.target);
        }
        if (!op.newText.isEmpty())
        {
            d.append(u" → "_ustr);
            d.append(clip(op.newText.replaceAll(u"\n"_ustr, u" "_ustr), 40));
        }
        d.append(u"\n"_ustr);
    }
    if (static_cast<sal_Int32>(rPlan.operations.size()) > 8)
        d.append(u"…共 "_ustr + OUString::number(static_cast<sal_Int32>(rPlan.operations.size()))
                 + u" 步\n"_ustr);

    d.append(u"\n打开对比（本机）：\n"_ustr);
    if (r.hasPre)
        d.append(u"  open \""_ustr + r.pre.path + u"\"\n"_ustr);
    if (r.hasPost)
        d.append(u"  open \""_ustr + r.post.path + u"\"\n"_ustr);
    d.append(u"可圈不把截图外传。\n"_ustr);
    return d.makeStringAndClear();
}

VisionEvidenceReport DocumentAIVisionEvidence::buildReport(const OUString& rPrePath,
                                                           const OUString& rPostPath,
                                                           const ApplyPlan& rPlan,
                                                           const DocumentAIApplyResult& rApply,
                                                           const OUString& rSurface)
{
    VisionEvidenceReport rep;
    rep.pre = statLocalFile(rPrePath);
    rep.post = statLocalFile(rPostPath);
    rep.hasPre = rep.pre.exists;
    rep.hasPost = rep.post.exists;
    rep.sizeDeltaBytes = (rep.hasPost ? rep.post.sizeBytes : 0)
                         - (rep.hasPre ? rep.pre.sizeBytes : 0);
    rep.mtimeDeltaSec = (rep.hasPost ? rep.post.mtimeSec : 0)
                        - (rep.hasPre ? rep.pre.mtimeSec : 0);
    rep.resolvedVisionModel = resolveLocalVisionModel();
    if (rep.hasPre)
        rep.preFingerprint = softFingerprint(rPrePath);
    if (rep.hasPost)
        rep.postFingerprint = softFingerprint(rPostPath);
    rep.fingerprintsMatch
        = rep.hasPre && rep.hasPost && !rep.preFingerprint.isEmpty()
          && rep.preFingerprint == rep.postFingerprint;

    if (!rep.hasPre && !rep.hasPost)
    {
        rep.status = u"skip"_ustr;
        rep.summaryZh = u"Vision 证据跳过 · 无本地截图"_ustr;
        rep.cardZh = rep.summaryZh + u" · 不上传"_ustr;
        rep.diffCardZh = rep.cardZh;
        return rep;
    }

    OUStringBuffer card;
    card.append(u"【写回视觉证据 · 仅本地 PNG · 不上传】\n"_ustr);
    card.append(u"计划："_ustr);
    card.append(rPlan.planId.isEmpty() ? u"（无 id）"_ustr : rPlan.planId);
    card.append(u" · 操作="_ustr);
    card.append(OUString::number(static_cast<sal_Int32>(rPlan.operations.size())));
    card.append(u" · applied="_ustr);
    card.append(OUString::number(rApply.appliedCount));
    card.append(u" · 引擎="_ustr);
    card.append(rApply.engine.isEmpty() ? u"?"_ustr : rApply.engine);
    if (!rSurface.isEmpty() || !rApply.surface.isEmpty())
    {
        card.append(u" · 表面="_ustr);
        card.append(!rApply.surface.isEmpty() ? rApply.surface : rSurface);
    }
    card.append(u"\n视觉模型："_ustr);
    card.append(rep.resolvedVisionModel.isEmpty() ? u"（未解析）"_ustr : rep.resolvedVisionModel);
    card.append(u"\n"_ustr);

    if (rep.hasPre)
    {
        card.append(u"pre： "_ustr);
        card.append(rep.pre.path);
        card.append(u" · "_ustr);
        card.append(OUString::number(rep.pre.sizeBytes));
        card.append(u" B\n"_ustr);
    }
    else
        card.append(u"pre： （缺失）\n"_ustr);

    if (rep.hasPost)
    {
        card.append(u"post："_ustr);
        card.append(rep.post.path);
        card.append(u" · "_ustr);
        card.append(OUString::number(rep.post.sizeBytes));
        card.append(u" B\n"_ustr);
    }
    else
        card.append(u"post：（缺失）\n"_ustr);

    card.append(u"体积差："_ustr);
    card.append(OUString::number(rep.sizeDeltaBytes));
    card.append(u" B · mtime差："_ustr);
    card.append(OUString::number(rep.mtimeDeltaSec));
    card.append(u" s\n"_ustr);
    if (!rep.preFingerprint.isEmpty() || !rep.postFingerprint.isEmpty())
    {
        card.append(u"软指纹一致："_ustr);
        card.append(rep.fingerprintsMatch ? u"是"_ustr : u"否"_ustr);
        card.append(u"\n"_ustr);
    }

    // Op preview for human audit
    sal_Int32 nShow = 0;
    for (const auto& op : rPlan.operations)
    {
        if (++nShow > 6)
            break;
        card.append(u"· "_ustr);
        card.append(op.opType);
        if (!op.target.isEmpty())
        {
            card.append(u" @"_ustr);
            card.append(op.target);
        }
        if (!op.newText.isEmpty())
        {
            card.append(u" → "_ustr);
            card.append(clip(op.newText.replaceAll(u"\n"_ustr, u" "_ustr), 48));
        }
        card.append(u"\n"_ustr);
    }
    if (static_cast<sal_Int32>(rPlan.operations.size()) > 6)
        card.append(u"· …共 "_ustr + OUString::number(static_cast<sal_Int32>(rPlan.operations.size()))
                    + u" 步\n"_ustr);

    card.append(u"\n提示：用系统看图工具打开 pre/post 对比；可圈不把截图外传。\n"_ustr);

    // Soft status
    if (rep.hasPre && rep.hasPost)
    {
        rep.status = u"soft-ok"_ustr;
        rep.summaryZh = u"Vision 证据齐全 · Δ="_ustr + OUString::number(rep.sizeDeltaBytes)
                        + u"B · 本地可对看"_ustr;
        if (rep.fingerprintsMatch && rApply.appliedCount > 0)
        {
            rep.status = u"soft-warn"_ustr;
            rep.summaryZh
                = u"Vision 警告 · pre/post 软指纹一致但 applied>0 · 请打开对比卡人工核对"_ustr;
        }
        else if (rep.sizeDeltaBytes == 0 && rApply.appliedCount > 0)
        {
            rep.status = u"soft-warn"_ustr;
            rep.summaryZh
                = u"Vision 警告 · 截图体积未变但 applied>0 · 请人工对看 pre/post"_ustr;
        }
    }
    else if (rep.hasPost)
    {
        rep.status = u"soft-warn"_ustr;
        rep.summaryZh = u"Vision 仅 post 截图 · 缺 pre"_ustr;
    }
    else
    {
        rep.status = u"soft-warn"_ustr;
        rep.summaryZh = u"Vision 仅 pre 截图 · 缺 post"_ustr;
    }

    rep.cardZh = card.makeStringAndClear();
    rep.diffCardZh = buildDiffCard(rep, rPlan, rApply);
    rep.modelCommentPrompt = buildDescribePrompt(rep, rPlan);
    return rep;
}

OUString DocumentAIVisionEvidence::formatStatusLine(const VisionEvidenceReport& r)
{
    return r.summaryZh.isEmpty() ? u"Vision 证据 · 无"_ustr : r.summaryZh;
}

OUString DocumentAIVisionEvidence::buildDescribePrompt(const VisionEvidenceReport& r,
                                                       const ApplyPlan& rPlan)
{
    OUStringBuffer b;
    b.append(u"你是可圈办公写回视觉审计助手。不要要求上传图片。\n"_ustr);
    b.append(u"根据下列「写回操作摘要」与本地截图元数据，用中文 2–4 句说明：\n"_ustr);
    b.append(u"1) 屏幕上预期应出现什么可见变化\n"_ustr);
    b.append(u"2) 若 pre/post 体积差为 0 可能意味着什么\n"_ustr);
    b.append(u"3) 用户应重点核对的 1 个位置\n"_ustr);
    b.append(u"禁止声称你已看到像素；你只有路径与操作文本。\n"_ustr);
    b.append(u"plan="_ustr);
    b.append(rPlan.planId);
    b.append(u" ops="_ustr);
    b.append(OUString::number(static_cast<sal_Int32>(rPlan.operations.size())));
    b.append(u"\n"_ustr);
    b.append(u"pre_exists="_ustr);
    b.append(r.hasPre ? u"1"_ustr : u"0"_ustr);
    b.append(u" post_exists="_ustr);
    b.append(r.hasPost ? u"1"_ustr : u"0"_ustr);
    b.append(u" size_delta_B="_ustr);
    b.append(OUString::number(r.sizeDeltaBytes));
    b.append(u"\n操作：\n"_ustr);
    sal_Int32 n = 0;
    for (const auto& op : rPlan.operations)
    {
        if (++n > 8)
            break;
        b.append(u"- "_ustr);
        b.append(op.opType);
        b.append(u" "_ustr);
        b.append(op.target);
        b.append(u" "_ustr);
        b.append(clip(op.newText, 40));
        b.append(u"\n"_ustr);
    }
    return b.makeStringAndClear();
}

OUString DocumentAIVisionEvidence::buildLocalMultimodalPrompt(const VisionEvidenceReport& r,
                                                              const ApplyPlan& rPlan)
{
    OUStringBuffer b;
    b.append(u"你是可圈办公写回视觉审计助手。下列图片是本机 pre/post 截图（按顺序），"
             u"请基于你看到的像素作答。图片不会上传公网。\n"_ustr);
    b.append(u"用中文 3–5 句说明：\n"_ustr);
    b.append(u"1) post 相对 pre 的可见变化（布局/文字/数字/图表）\n"_ustr);
    b.append(u"2) 是否与写回操作意图一致\n"_ustr);
    b.append(u"3) 若无明显差异，提示用户人工核对的位置\n"_ustr);
    b.append(u"plan="_ustr);
    b.append(rPlan.planId);
    b.append(u" ops="_ustr);
    b.append(OUString::number(static_cast<sal_Int32>(rPlan.operations.size())));
    b.append(u" size_delta_B="_ustr);
    b.append(OUString::number(r.sizeDeltaBytes));
    b.append(u"\n操作摘要：\n"_ustr);
    sal_Int32 n = 0;
    for (const auto& op : rPlan.operations)
    {
        if (++n > 8)
            break;
        b.append(u"- "_ustr);
        b.append(op.opType);
        b.append(u" "_ustr);
        b.append(op.target);
        b.append(u" "_ustr);
        b.append(clip(op.newText, 48));
        b.append(u"\n"_ustr);
    }
    return b.makeStringAndClear();
}

LocalVisionDescribeResult DocumentAIVisionEvidence::describeWithLocalImages(
    const VisionEvidenceReport& r, const ApplyPlan& rPlan, const OUString& rVisionModel,
    const OUString& rVisionCmd)
{
    LocalVisionDescribeResult out;
    out.publicNetworkAttempted = false;
    if (!r.hasPre && !r.hasPost)
    {
        out.status = u"skip"_ustr;
        out.backend = u"none"_ustr;
        out.contentZh = u"无本地截图，跳过多模态视觉审计"_ustr;
        return out;
    }

    const OUString prompt = buildLocalMultimodalPrompt(r, rPlan);

    // 1) External local command (user-controlled, still local paths only).
    OUString cmd = rVisionCmd;
    if (cmd.isEmpty())
        cmd = envOrEmpty("KQOFFICE_AI_VISION_CMD");
    if (!cmd.isEmpty())
    {
        const OUString text = runVisionCmd(cmd, r.pre.path, r.post.path, prompt);
        if (!text.isEmpty())
        {
            out.status = u"ok"_ustr;
            out.usedLocalImages = true;
            out.backend = u"vision-cmd"_ustr;
            out.modelHint = u"cmd"_ustr;
            out.contentZh = text;
            return out;
        }
        SAL_INFO("kqoffice.ai.vision", "vision-cmd produced empty; trying ollama local");
    }

    // 2) Ollama loopback multimodal (/api/chat + images base64).
    std::vector<std::string> images;
    if (r.hasPre)
    {
        std::vector<char> bytes;
        if (readFileBytes(r.pre.path, bytes, kMaxVisionFileBytes))
            images.push_back(base64Encode(bytes));
    }
    if (r.hasPost)
    {
        std::vector<char> bytes;
        if (readFileBytes(r.post.path, bytes, kMaxVisionFileBytes))
            images.push_back(base64Encode(bytes));
    }
    if (images.empty())
    {
        out.status = u"error"_ustr;
        out.backend = u"none"_ustr;
        out.contentZh = u"本地截图读取失败或超过 2MiB · 未调用模型 · 可回退文字审计"_ustr;
        return out;
    }

    const OUString model = resolveLocalVisionModel(rVisionModel);

    const OUString answer = ollamaChatLocalVision(model, prompt, images);
    if (!answer.isEmpty())
    {
        out.status = u"ok"_ustr;
        out.usedLocalImages = true;
        out.backend = u"ollama-local"_ustr;
        out.modelHint = model;
        out.contentZh = answer;
        return out;
    }

    out.status = u"text-fallback"_ustr;
    out.usedLocalImages = false;
    out.backend = u"none"_ustr;
    out.modelHint = model;
    out.contentZh
        = u"本机多模态不可用（Ollama 未响应或无视觉模型 "_ustr + model
          + u"）· 将回退文字审计 · 截图仍仅本地 · 未公网上传"_ustr;
    return out;
}

OUString DocumentAIVisionEvidence::resolveLocalVisionModel(const OUString& rPreferred)
{
    kqoffice::ai::ModelRoutingSnapshot routing;
    try
    {
        routing = kqoffice::ai::loadModelRoutingSnapshot();
    }
    catch (...)
    {
        // never throw out
    }
    std::vector<OUString> installed;
    try
    {
        kqoffice::ai::OllamaAdapter ollama;
        installed = ollama.listModels();
    }
    catch (...)
    {
    }
    return kqoffice::ai::resolveVisionModel(rPreferred, routing, installed);
}

OUString DocumentAIVisionEvidence::formatVisionRouteStatusZh(const OUString& rPreferred)
{
    kqoffice::ai::ModelRoutingSnapshot routing;
    try
    {
        routing = kqoffice::ai::loadModelRoutingSnapshot();
    }
    catch (...)
    {
    }
    std::vector<OUString> installed;
    bool ollamaUp = false;
    try
    {
        kqoffice::ai::OllamaAdapter ollama;
        ollamaUp = ollama.probe() == u"reachable"_ustr;
        installed = ollama.listModels();
    }
    catch (...)
    {
    }

    const OUString resolved = kqoffice::ai::resolveVisionModel(rPreferred, routing, installed);
    OUString source = u"default/llava"_ustr;
    if (!rPreferred.isEmpty())
        source = u"preferred/arg"_ustr;
    else if (!routing.visionModel.isEmpty())
        source = u"routing.visionModel"_ustr;
    else if (envOrEmpty("KQOFFICE_AI_VISION_MODEL").getLength() > 0)
        source = u"env:KQOFFICE_AI_VISION_MODEL"_ustr;
    else
    {
        for (const auto& m : installed)
        {
            const OUString n = m.toAsciiLowerCase();
            if (n.indexOf(u"llava"_ustr) >= 0 || n.indexOf(u"vision"_ustr) >= 0
                || (n.indexOf(u"qwen"_ustr) >= 0 && n.indexOf(u"vl"_ustr) >= 0))
            {
                source = u"installed-vision-ish"_ustr;
                break;
            }
        }
        if (source == u"default/llava"_ustr && !routing.lightModel.isEmpty()
            && resolved == routing.lightModel)
            source = u"fallback:light"_ustr;
        else if (source == u"default/llava"_ustr && !routing.primaryModel.isEmpty()
                 && resolved == routing.primaryModel)
            source = u"fallback:primary"_ustr;
    }

    OUStringBuffer b;
    b.append(u"Vision 路由（本地）\n"_ustr);
    b.append(u"· 解析模型："_ustr);
    b.append(resolved.isEmpty() ? u"（空）"_ustr : resolved);
    b.append(u"\n· 来源："_ustr);
    b.append(source);
    b.append(u"\n· Ollama："_ustr);
    b.append(ollamaUp ? u"可达 · 127.0.0.1:11434"_ustr : u"不可达/未启动"_ustr);
    b.append(u"\n· 已装模型数："_ustr);
    b.append(OUString::number(static_cast<sal_Int32>(installed.size())));
    b.append(u"\n· 多模态偏好：applyVisionLocalMultimodal（默认开）· 截图不上传公网\n"_ustr);
    b.append(u"· 侧栏：`/vision-status` 可随时查看\n"_ustr);
    return b.makeStringAndClear();
}

} // namespace kqoffice::ai::chat

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
