/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1 / Wave D5: AI workspace content opener).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatContentOpener.hxx"

#include "AIChatContentObjectStore.hxx"
#include "AIChatMarkdownRenderer.hxx"

#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/system/SystemShellExecute.hpp>
#include <com/sun/star/system/SystemShellExecuteFlags.hpp>
#include <com/sun/star/uno/Reference.hxx>
#include <com/sun/star/uno/Sequence.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/propertyvalue.hxx>
#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <vector>

namespace sfx2::sidebar
{
namespace
{
constexpr sal_Int32 MAX_TEXT_PREVIEW_CHARS = 12000;
constexpr sal_uInt64 MAX_TEXT_PREVIEW_BYTES = 256 * 1024;

bool ReadUtf8FileLimited(const OUString& rUrl, OUString& rText)
{
    osl::File aFile(rUrl);
    if (aFile.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;

    sal_uInt64 nSize = 0;
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize == 0)
    {
        aFile.close();
        return false;
    }
    if (nSize > MAX_TEXT_PREVIEW_BYTES)
        nSize = MAX_TEXT_PREVIEW_BYTES;

    std::vector<char> aBuffer(static_cast<size_t>(nSize));
    sal_uInt64 nRead = 0;
    if (aFile.read(aBuffer.data(), nSize, nRead) != osl::FileBase::E_None || nRead == 0)
    {
        aFile.close();
        return false;
    }
    aFile.close();

    rText = OStringToOUString(OString(aBuffer.data(), static_cast<sal_Int32>(nRead)),
                              RTL_TEXTENCODING_UTF8);
    if (rText.getLength() > MAX_TEXT_PREVIEW_CHARS)
        rText = rText.copy(0, MAX_TEXT_PREVIEW_CHARS) + u"\n…（预览已截断）"_ustr;
    return !rText.isEmpty();
}

OUString EnsureFileUrl(const OUString& rPathOrUrl)
{
    if (rPathOrUrl.isEmpty())
        return {};
    if (rPathOrUrl.startsWith(u"file:"_ustr))
        return rPathOrUrl;
    return AIChatPreviewMatrix::ToFileUrl(rPathOrUrl);
}
} // namespace

OUString AIChatContentOpener::ResolveOpenTarget(const AIChatContentRegistryEntry& rEntry)
{
    return AIChatPreviewMatrix::ResolvePreviewTarget(rEntry);
}

bool AIChatContentOpener::IsSupportedTarget(const OUString& rTarget)
{
    return rTarget == u"main-document-window"_ustr || rTarget == u"sidebar-preview"_ustr
           || rTarget == u"diff-review"_ustr || rTarget == u"evidence-inspector"_ustr
           || rTarget == u"review-queue"_ustr;
}

bool AIChatContentOpener::OpenWithLocalFilters(const OUString& rFileUrl)
{
    if (rFileUrl.isEmpty())
        return false;
    try
    {
        css::uno::Reference<css::frame::XDesktop2> xDesktop
            = css::frame::Desktop::create(comphelper::getProcessComponentContext());
        if (!xDesktop.is())
            return false;

        const css::uno::Sequence<css::beans::PropertyValue> aArgs{
            comphelper::makePropertyValue(u"ReadOnly"_ustr, true),
            comphelper::makePropertyValue(u"AsTemplate"_ustr, false),
            comphelper::makePropertyValue(u"Referer"_ustr, u"private:user"_ustr),
        };

        css::uno::Reference<css::lang::XComponent> xComp = xDesktop->loadComponentFromURL(
            rFileUrl, u"_default"_ustr, 0, aArgs);
        return xComp.is();
    }
    catch (...)
    {
        return false;
    }
}

bool AIChatContentOpener::OpenWithSystemOrFilters(const OUString& rFileUrl)
{
    if (rFileUrl.isEmpty())
        return false;
    // Prefer LO filters first (same process, better for office-ish unknowns).
    if (OpenWithLocalFilters(rFileUrl))
        return true;
    try
    {
        css::uno::Reference<css::system::XSystemShellExecute> xExec(
            css::system::SystemShellExecute::create(comphelper::getProcessComponentContext()));
        if (!xExec.is())
            return false;
        xExec->execute(rFileUrl, OUString(), css::system::SystemShellExecuteFlags::URIS_ONLY);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool AIChatContentOpener::LoadTextPreview(const AIChatContentRegistryEntry& rEntry,
                                             const AIChatPreviewResult& rPreview, OUString& rBody,
                                             OUString& rDetailMessage)
{
    rBody.clear();
    rDetailMessage.clear();

    // 1) Direct filesystem path.
    if (!rPreview.FilePath.isEmpty())
    {
        const OUString sUrl = !rPreview.FileUrl.isEmpty() ? rPreview.FileUrl
                                                          : EnsureFileUrl(rPreview.FilePath);
        if (!sUrl.isEmpty() && ReadUtf8FileLimited(sUrl, rBody))
        {
            if (rPreview.FileKind == AIChatPreviewFileKind::Markdown)
            {
                const AIChatMarkdownRenderResult aMd = RenderMarkdownSubset(rBody);
                if (!aMd.Rejected && !aMd.Text.isEmpty())
                    rBody = aMd.Text;
            }
            rDetailMessage = u"侧栏只读预览 · "_ustr + rPreview.FilePath;
            return true;
        }
        rDetailMessage = u"无法读取文件内容 · "_ustr + rPreview.FilePath;
        return false;
    }

    // 2) Materialized content-object sidecar (@artifact:<id>).
    const OUString sObjectId = AIChatPreviewMatrix::ResolveContentObjectId(rEntry);
    if (!sObjectId.isEmpty())
    {
        AIChatContentObjectStore aStore;
        OUString sText;
        if (aStore.ReadObjectText(sObjectId, sText) && !sText.isEmpty())
        {
            if (sText.getLength() > MAX_TEXT_PREVIEW_CHARS)
                sText = sText.copy(0, MAX_TEXT_PREVIEW_CHARS) + u"\n…（预览已截断）"_ustr;
            if (rPreview.FileKind == AIChatPreviewFileKind::Markdown
                || rEntry.Type == u"structured-text"_ustr
                || rEntry.Type == u"document-tool-read"_ustr
                || rEntry.Type == u"document-tool-context"_ustr
                || rEntry.Type == u"knowledge-index-result"_ustr)
            {
                const AIChatMarkdownRenderResult aMd = RenderMarkdownSubset(sText);
                if (!aMd.Rejected && !aMd.Text.isEmpty())
                    sText = aMd.Text;
            }
            rBody = sText;
            rDetailMessage = u"侧栏只读预览 · 内容对象 "_ustr + sObjectId
                             + u" · 主文档未改"_ustr;
            return true;
        }
    }

    // 3) Text-ish registry entry without body storage (assistant-output, etc.).
    if (rPreview.FileKind == AIChatPreviewFileKind::Text
        || rPreview.FileKind == AIChatPreviewFileKind::Markdown
        || rEntry.PreviewMode == u"text-preview"_ustr
        || rEntry.PreviewMode == u"read-only-preview"_ustr
        || rEntry.Type == u"assistant-output"_ustr
        || rEntry.Type == u"document-tool-context"_ustr
        || rEntry.Type == u"document-tool-read"_ustr
        || rEntry.Type == u"knowledge-index-result"_ustr)
    {
        rDetailMessage
            = u"文本预览：侧栏只读 · 不改主文档 · 类型="_ustr + rEntry.Type;
        rBody = u"（无独立文件正文或 sidecar 未找到）\n标识："_ustr + rEntry.ObjectId
                + u"\n类型："_ustr + rEntry.Type + u"\n来源："_ustr + rEntry.SourceSurface
                + u"\n引用："_ustr + rEntry.HashReference
                + u"\n证据："_ustr + rEntry.EvidenceId;
        return true;
    }

    return false;
}

AIChatContentOpenResult
AIChatContentOpener::OpenReadOnlyPreview(const AIChatContentRegistryEntry& rEntry) const
{
    AIChatPreviewMatrix aPreviewMatrix;
    const AIChatPreviewResult aPreview = aPreviewMatrix.BuildPreview(rEntry);

    AIChatContentOpenResult aResult;
    aResult.ObjectId = rEntry.ObjectId;
    aResult.Target = aPreview.Target;
    aResult.PreviewMode = aPreview.Mode;
    aResult.PreviewSummary = aPreview.Summary;
    aResult.FileKind = aPreview.FileKind;
    aResult.FileKindLabel = aPreview.FileKindLabel;
    aResult.FilePath = aPreview.FilePath;
    aResult.FileUrl = aPreview.FileUrl;
    aResult.UserMessage = aPreview.UserMessage;

    if (rEntry.ObjectId.isEmpty())
    {
        aResult.UserMessage = u"打开失败：缺少内容标识"_ustr;
        aResult.Message = u"open-failed reason=missing-object-id · "_ustr + aResult.UserMessage;
        return aResult;
    }

    if (!IsSupportedTarget(aResult.Target))
    {
        aResult.UserMessage = u"打开失败：不支持的目标 "_ustr + aResult.Target;
        aResult.Message = u"open-failed reason=unsupported-target target="_ustr + aResult.Target
                          + u" · "_ustr + aResult.UserMessage;
        return aResult;
    }

    // ── Wave D5: execute preview by file kind ─────────────────────────
    switch (aPreview.FileKind)
    {
        case AIChatPreviewFileKind::Markdown:
        case AIChatPreviewFileKind::Text:
        {
            OUString sBody;
            OUString sDetail;
            const bool bLoaded = LoadTextPreview(rEntry, aPreview, sBody, sDetail);
            aResult.PreviewText = sBody;
            if (bLoaded)
            {
                aResult.Success = true;
                aResult.UserMessage
                    = sDetail.isEmpty() ? u"已在侧栏打开只读预览"_ustr : sDetail;
                aResult.Target = u"sidebar-preview"_ustr;
                aResult.PreviewMode = u"read-only-preview"_ustr;
            }
            else
            {
                aResult.Success = false;
                aResult.UserMessage
                    = sDetail.isEmpty() ? u"文本预览失败"_ustr : sDetail;
            }
            break;
        }
        case AIChatPreviewFileKind::OfficeDocument:
        {
            const OUString sUrl
                = !aPreview.FileUrl.isEmpty() ? aPreview.FileUrl : EnsureFileUrl(aPreview.FilePath);
            if (sUrl.isEmpty())
            {
                aResult.Success = false;
                aResult.UserMessage = u"打开失败：无法解析文件路径"_ustr;
            }
            else if (OpenWithLocalFilters(sUrl))
            {
                aResult.Success = true;
                aResult.UserMessage = u"已用本地应用打开预览"_ustr;
                aResult.Target = u"main-document-window"_ustr;
                aResult.PreviewMode = u"local-app-preview"_ustr;
            }
            else
            {
                aResult.Success = false;
                aResult.UserMessage = u"本地应用打开预览失败 · "_ustr
                                      + (aPreview.FilePath.isEmpty() ? sUrl : aPreview.FilePath);
            }
            break;
        }
        case AIChatPreviewFileKind::Image:
        {
            const OUString sUrl
                = !aPreview.FileUrl.isEmpty() ? aPreview.FileUrl : EnsureFileUrl(aPreview.FilePath);
            if (sUrl.isEmpty())
            {
                aResult.Success = false;
                aResult.UserMessage = u"打开失败：无法解析图片路径"_ustr;
            }
            else if (OpenWithSystemOrFilters(sUrl))
            {
                aResult.Success = true;
                aResult.UserMessage = u"已用本地应用打开图片预览（未上传）"_ustr;
                aResult.Target = u"main-document-window"_ustr;
                aResult.PreviewMode = u"image-preview"_ustr;
            }
            else
            {
                aResult.Success = false;
                aResult.UserMessage = u"图片打开失败 · "_ustr
                                      + (aPreview.FilePath.isEmpty() ? sUrl : aPreview.FilePath);
            }
            break;
        }
        case AIChatPreviewFileKind::Unsupported:
        {
            // Still offer open: try LO filters then system shell.
            const OUString sUrl
                = !aPreview.FileUrl.isEmpty() ? aPreview.FileUrl : EnsureFileUrl(aPreview.FilePath);
            if (sUrl.isEmpty())
            {
                aResult.Success = true; // fail-closed visible, but "offer 打开" is informational
                aResult.UserMessage
                    = u"暂不支持内嵌预览，且未找到可打开路径；可检查产物路径后重试「打开」"_ustr;
            }
            else if (OpenWithSystemOrFilters(sUrl))
            {
                aResult.Success = true;
                aResult.UserMessage
                    = u"暂不支持内嵌预览，已尝试用本地应用打开"_ustr;
                aResult.Target = u"main-document-window"_ustr;
                aResult.PreviewMode = u"unsupported-preview"_ustr;
            }
            else
            {
                aResult.Success = false;
                aResult.UserMessage
                    = u"暂不支持内嵌预览，打开也失败；请确认文件仍存在后重试「打开」"_ustr;
            }
            break;
        }
        case AIChatPreviewFileKind::Metadata:
        default:
        {
            // Existing registry / review / evidence paths: metadata-only open succeeds
            // without mutating the main document.
            aResult.Success = true;
            if (aResult.UserMessage.isEmpty())
                aResult.UserMessage = u"已打开元数据预览（只读）"_ustr;
            break;
        }
    }

    OUStringBuffer aMessage;
    if (aResult.Success)
    {
        aMessage.append(u"opened id="_ustr + rEntry.ObjectId + u" target="_ustr + aResult.Target
                        + u" preview-mode="_ustr + aResult.PreviewMode
                        + u" file-kind="_ustr + aResult.FileKindLabel
                        + u" preview-matrix=true"_ustr
                        + u" read-only=true main-document-mutation=false"_ustr);
        if (!aResult.FilePath.isEmpty())
            aMessage.append(u" file-path="_ustr + aResult.FilePath);
    }
    else
    {
        aMessage.append(u"open-failed id="_ustr + rEntry.ObjectId + u" target="_ustr
                        + aResult.Target + u" file-kind="_ustr + aResult.FileKindLabel);
        if (!aResult.FilePath.isEmpty())
            aMessage.append(u" file-path="_ustr + aResult.FilePath);
    }
    aMessage.append(u" · "_ustr + aResult.UserMessage);
    aResult.Message = aMessage.makeStringAndClear();

    // Enrich PreviewSummary for details panel.
    if (!aResult.PreviewText.isEmpty())
    {
        aResult.PreviewSummary = aPreview.Summary + u"\n\n—— 预览正文 ——\n"_ustr
                                 + aResult.PreviewText;
    }
    else if (!aResult.UserMessage.isEmpty())
    {
        aResult.PreviewSummary = aPreview.Summary + u" · "_ustr + aResult.UserMessage;
    }

    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
