/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1 / Wave D5: AI workspace preview matrix).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatPreviewMatrix.hxx"

#include "AIChatReviewQueueStore.hxx"
#include "AIChatReviewStateSyncStore.hxx"
#include "AIChatSourceProvenance.hxx"

#include <osl/file.hxx>
#include <rtl/ustrbuf.hxx>

namespace sfx2::sidebar
{
namespace
{
OUString ExtensionOf(const OUString& rPathOrName)
{
    if (rPathOrName.isEmpty())
        return {};
    sal_Int32 nSlash = rPathOrName.lastIndexOf('/');
    const sal_Int32 nBSlash = rPathOrName.lastIndexOf('\\');
    if (nBSlash > nSlash)
        nSlash = nBSlash;
    const OUString sName
        = nSlash >= 0 ? rPathOrName.copy(nSlash + 1) : rPathOrName;
    const sal_Int32 nDot = sName.lastIndexOf('.');
    if (nDot < 0 || nDot + 1 >= sName.getLength())
        return {};
    return sName.copy(nDot + 1).toAsciiLowerCase();
}

bool LooksLikeAbsolutePath(const OUString& rValue)
{
    if (rValue.isEmpty())
        return false;
    if (rValue.startsWith(u"/"_ustr))
        return true;
    // Windows drive path: C:\... or C:/...
    return rValue.getLength() > 2 && ((rValue[0] >= 'A' && rValue[0] <= 'Z')
                                      || (rValue[0] >= 'a' && rValue[0] <= 'z'))
           && rValue[1] == ':'
           && (rValue[2] == '\\' || rValue[2] == '/');
}

OUString StripPathPrefix(OUString sValue)
{
    sValue = sValue.trim();
    if (sValue.startsWith(u"@file:"_ustr))
        return sValue.copy(6).trim();
    if (sValue.startsWith(u"file-path:"_ustr))
        return sValue.copy(10).trim();
    if (sValue.startsWith(u"path:"_ustr))
        return sValue.copy(5).trim();
    return sValue;
}

OUString TryParsePathCandidate(const OUString& rRaw)
{
    if (rRaw.isEmpty())
        return {};

    const OUString sValue = StripPathPrefix(rRaw);
    if (sValue.isEmpty())
        return {};

    // Hash / artifact refs are not filesystem paths.
    if (sValue.startsWith(u"hash:"_ustr) || sValue.startsWith(u"@artifact:"_ustr)
        || sValue.startsWith(u"evidence:"_ustr) || sValue.startsWith(u"citation:"_ustr)
        || sValue.startsWith(u"source:"_ustr) || sValue.startsWith(u"review:"_ustr))
        return {};

    if (sValue.startsWith(u"file:"_ustr))
    {
        OUString sSys;
        if (osl::FileBase::getSystemPathFromFileURL(sValue, sSys) == osl::FileBase::E_None
            && !sSys.isEmpty())
            return sSys;
        return {};
    }

    if (LooksLikeAbsolutePath(sValue))
        return sValue;

    // Relative path with a known extension (task workspace files).
    const OUString sExt = ExtensionOf(sValue);
    if (!sExt.isEmpty()
        && (sValue.indexOf('/') >= 0 || sValue.indexOf('\\') >= 0 || sValue.indexOf('.') >= 0))
    {
        // Reject pure type tokens like "document" without a separator/dot path shape.
        if (sValue.indexOf('/') >= 0 || sValue.indexOf('\\') >= 0)
            return sValue;
        // bare filename.ext — accept for kind detection via ObjectId/HashReference.
        if (sValue.indexOf('.') >= 0 && sValue.indexOf(' ') < 0)
            return sValue;
    }
    return {};
}
} // namespace

OUString AIChatPreviewMatrix::ToFileUrl(const OUString& rSystemPathOrUrl)
{
    if (rSystemPathOrUrl.isEmpty())
        return {};
    if (rSystemPathOrUrl.startsWith(u"file:"_ustr))
        return rSystemPathOrUrl;
    OUString sUrl;
    if (osl::FileBase::getFileURLFromSystemPath(rSystemPathOrUrl, sUrl) == osl::FileBase::E_None)
        return sUrl;
    return {};
}

OUString AIChatPreviewMatrix::ResolveArtifactFilePath(const AIChatContentRegistryEntry& rEntry)
{
    if (OUString s = TryParsePathCandidate(rEntry.SourceSurface); !s.isEmpty())
        return s;
    if (OUString s = TryParsePathCandidate(rEntry.HashReference); !s.isEmpty())
        return s;
    if (OUString s = TryParsePathCandidate(rEntry.ObjectId); !s.isEmpty())
        return s;
    return {};
}

OUString AIChatPreviewMatrix::ResolveContentObjectId(const AIChatContentRegistryEntry& rEntry)
{
    const OUString sRef = rEntry.HashReference;
    constexpr OUStringLiteral PREFIX = u"@artifact:";
    if (sRef.startsWith(PREFIX))
        return sRef.copy(OUString(PREFIX).getLength());
    // ObjectId itself may be the content-object hash (materialized paste).
    if (rEntry.Type == u"plain-text-large"_ustr || rEntry.Type == u"structured-text"_ustr)
        return rEntry.ObjectId;
    return {};
}

OUString AIChatPreviewMatrix::FileKindToLabel(AIChatPreviewFileKind eKind)
{
    switch (eKind)
    {
        case AIChatPreviewFileKind::Text:
            return u"text"_ustr;
        case AIChatPreviewFileKind::Markdown:
            return u"markdown"_ustr;
        case AIChatPreviewFileKind::OfficeDocument:
            return u"office-document"_ustr;
        case AIChatPreviewFileKind::Image:
            return u"image"_ustr;
        case AIChatPreviewFileKind::Unsupported:
            return u"unsupported"_ustr;
        case AIChatPreviewFileKind::Metadata:
        default:
            return u"metadata"_ustr;
    }
}

OUString AIChatPreviewMatrix::FileKindToLabelZh(AIChatPreviewFileKind eKind)
{
    switch (eKind)
    {
        case AIChatPreviewFileKind::Text:
            return u"纯文本"_ustr;
        case AIChatPreviewFileKind::Markdown:
            return u"Markdown"_ustr;
        case AIChatPreviewFileKind::OfficeDocument:
            return u"办公文档/PDF"_ustr;
        case AIChatPreviewFileKind::Image:
            return u"图片"_ustr;
        case AIChatPreviewFileKind::Unsupported:
            return u"暂不支持内嵌预览"_ustr;
        case AIChatPreviewFileKind::Metadata:
        default:
            return u"元数据"_ustr;
    }
}

AIChatPreviewFileKind
AIChatPreviewMatrix::DetectFileKind(const OUString& rPathOrName,
                                    const AIChatContentRegistryEntry& rEntry)
{
    const OUString sExt = ExtensionOf(rPathOrName);
    if (sExt == u"md"_ustr || sExt == u"markdown"_ustr)
        return AIChatPreviewFileKind::Markdown;
    if (sExt == u"txt"_ustr || sExt == u"log"_ustr || sExt == u"csv"_ustr || sExt == u"tsv"_ustr
        || sExt == u"json"_ustr || sExt == u"xml"_ustr || sExt == u"yaml"_ustr
        || sExt == u"yml"_ustr || sExt == u"ini"_ustr || sExt == u"cfg"_ustr
        || sExt == u"conf"_ustr || sExt == u"html"_ustr || sExt == u"htm"_ustr
        || sExt == u"css"_ustr || sExt == u"js"_ustr || sExt == u"ts"_ustr
        || sExt == u"py"_ustr || sExt == u"sh"_ustr || sExt == u"c"_ustr || sExt == u"cpp"_ustr
        || sExt == u"h"_ustr || sExt == u"hpp"_ustr || sExt == u"java"_ustr
        || sExt == u"go"_ustr || sExt == u"rs"_ustr || sExt == u"rb"_ustr)
        return AIChatPreviewFileKind::Text;
    // LO-filter office / PDF (D5 primary: pdf / docx / odt)
    if (sExt == u"pdf"_ustr || sExt == u"docx"_ustr || sExt == u"odt"_ustr || sExt == u"doc"_ustr
        || sExt == u"rtf"_ustr || sExt == u"ods"_ustr || sExt == u"xlsx"_ustr
        || sExt == u"xls"_ustr || sExt == u"odp"_ustr || sExt == u"pptx"_ustr
        || sExt == u"ppt"_ustr || sExt == u"odg"_ustr || sExt == u"fodt"_ustr
        || sExt == u"fods"_ustr || sExt == u"fodp"_ustr)
        return AIChatPreviewFileKind::OfficeDocument;

    // Screenshots / attached images (D5: open via system or LO Draw when possible)
    if (sExt == u"png"_ustr || sExt == u"jpg"_ustr || sExt == u"jpeg"_ustr
        || sExt == u"gif"_ustr || sExt == u"webp"_ustr || sExt == u"bmp"_ustr
        || sExt == u"svg"_ustr || sExt == u"tif"_ustr || sExt == u"tiff"_ustr)
        return AIChatPreviewFileKind::Image;

    if (!rPathOrName.isEmpty())
        return AIChatPreviewFileKind::Unsupported;

    // No path: infer soft kinds from registry type / preview mode for text-ish artifacts.
    if (rEntry.PreviewMode == u"text-preview"_ustr || rEntry.Type == u"assistant-output"_ustr
        || rEntry.Type == u"plain-text-large"_ustr || rEntry.Type == u"structured-text"_ustr)
        return AIChatPreviewFileKind::Text;

    return AIChatPreviewFileKind::Metadata;
}

OUString AIChatPreviewMatrix::ResolvePreviewTarget(const AIChatContentRegistryEntry& rEntry)
{
    const OUString sPath = ResolveArtifactFilePath(rEntry);
    const AIChatPreviewFileKind eKind = DetectFileKind(sPath, rEntry);

    // File-kind routing takes precedence for DuMate-style 任务文件 preview (Wave D5).
    if (eKind == AIChatPreviewFileKind::OfficeDocument
        || eKind == AIChatPreviewFileKind::Image)
        return u"main-document-window"_ustr;
    if (eKind == AIChatPreviewFileKind::Text || eKind == AIChatPreviewFileKind::Markdown
        || eKind == AIChatPreviewFileKind::Unsupported)
        return u"sidebar-preview"_ustr;

    if (!rEntry.OpenTarget.isEmpty())
    {
        // Alias historical / informal targets onto supported surfaces.
        if (rEntry.OpenTarget == u"preview"_ustr || rEntry.OpenTarget == u"text-preview"_ustr
            || rEntry.OpenTarget == u"read-only-preview"_ustr)
            return u"sidebar-preview"_ustr;
        if (rEntry.OpenTarget == u"local-app-preview"_ustr
            || rEntry.OpenTarget == u"local-preview"_ustr)
            return u"main-document-window"_ustr;
        return rEntry.OpenTarget;
    }

    if (rEntry.Type == u"document"_ustr)
        return u"main-document-window"_ustr;
    if (rEntry.Type == u"task-step"_ustr || rEntry.Type == u"review-item"_ustr
        || rEntry.Type == u"formatting-preview"_ustr)
        return u"diff-review"_ustr;
    if (rEntry.Type == u"evidence-record"_ustr)
        return u"sidebar-preview"_ustr;
    if (rEntry.Type == u"selection"_ustr || rEntry.Type == u"connector-result"_ustr
        || rEntry.Type == u"knowledge-index-result"_ustr)
        return u"sidebar-preview"_ustr;

    return u"sidebar-preview"_ustr;
}

OUString AIChatPreviewMatrix::ResolvePreviewMode(const AIChatContentRegistryEntry& rEntry)
{
    const OUString sPath = ResolveArtifactFilePath(rEntry);
    const AIChatPreviewFileKind eKind = DetectFileKind(sPath, rEntry);

    if (eKind == AIChatPreviewFileKind::Markdown || eKind == AIChatPreviewFileKind::Text)
        return u"read-only-preview"_ustr;
    if (eKind == AIChatPreviewFileKind::OfficeDocument)
        return u"local-app-preview"_ustr;
    if (eKind == AIChatPreviewFileKind::Image)
        return u"image-preview"_ustr;
    if (eKind == AIChatPreviewFileKind::Unsupported)
        return u"unsupported-preview"_ustr;

    if (!rEntry.PreviewMode.isEmpty())
    {
        if (rEntry.PreviewMode == u"text-preview"_ustr)
            return u"read-only-preview"_ustr;
        return rEntry.PreviewMode;
    }

    if (rEntry.Type == u"task-step"_ustr || rEntry.Type == u"review-item"_ustr
        || rEntry.Type == u"formatting-preview"_ustr)
        return u"diff-preview"_ustr;
    if (rEntry.Type == u"evidence-record"_ustr)
        return u"evidence-summary"_ustr;
    return u"metadata-summary"_ustr;
}

bool AIChatPreviewMatrix::IsSupportedPreviewTarget(const OUString& rTarget)
{
    return rTarget == u"main-document-window"_ustr || rTarget == u"sidebar-preview"_ustr
           || rTarget == u"diff-review"_ustr || rTarget == u"evidence-inspector"_ustr
           || rTarget == u"review-queue"_ustr;
}

AIChatPreviewResult AIChatPreviewMatrix::BuildPreview(const AIChatContentRegistryEntry& rEntry) const
{
    AIChatPreviewResult aResult;
    aResult.ObjectId = rEntry.ObjectId;
    aResult.ContentType = rEntry.Type;
    aResult.FilePath = ResolveArtifactFilePath(rEntry);
    aResult.FileUrl = ToFileUrl(aResult.FilePath);
    aResult.FileKind = DetectFileKind(aResult.FilePath, rEntry);
    // Content-object sidecars without a path are still text previews.
    if (aResult.FileKind == AIChatPreviewFileKind::Metadata
        && !ResolveContentObjectId(rEntry).isEmpty())
        aResult.FileKind = AIChatPreviewFileKind::Text;
    aResult.FileKindLabel = FileKindToLabel(aResult.FileKind);
    aResult.Target = ResolvePreviewTarget(rEntry);
    aResult.Mode = ResolvePreviewMode(rEntry);
    aResult.EvidenceBadge = rEntry.EvidenceId.isEmpty() ? u"evidence=missing"_ustr
                                                        : u"evidence=linked"_ustr;
    aResult.SourceMetadata = u"source-id="_ustr
                             + AIChatSourceProvenance::MakeSourceId(rEntry.ObjectId)
                             + u" citation-id="_ustr
                             + AIChatSourceProvenance::MakeCitationId(rEntry.ObjectId)
                             + u" evidence-id="_ustr + rEntry.EvidenceId + u" source="_ustr
                             + rEntry.SourceSurface + u" hash="_ustr + rEntry.HashReference;
    if (!aResult.FilePath.isEmpty())
        aResult.SourceMetadata += u" file-path="_ustr + aResult.FilePath;
    aResult.SourceMetadata += u" file-kind="_ustr + aResult.FileKindLabel;

    if (AIChatReviewQueueStore::IsReviewQueueEntry(rEntry) && !rEntry.EvidenceId.isEmpty()
        && !rEntry.HashReference.isEmpty())
    {
        AIChatReviewStateSyncStore aStateSync;
        AIChatReviewStateSyncResult aSync = aStateSync.GetLatestState(rEntry.ObjectId);
        if (!aSync.Success)
        {
            const OUString sState = AIChatReviewStateSyncStore::NormalizeRegistryState(rEntry.State);
            aSync = aStateSync.RecordFromRegistry(
                rEntry, AIChatReviewStateSyncStore::TransitionForState(sState), sState,
                u"preview-matrix"_ustr);
        }
        if (aSync.Success)
            aResult.SourceMetadata += u" "_ustr + aSync.Entry.VisibleState;
        else
            aResult.SourceMetadata += u" review-state-sync-failed="_ustr + aSync.Message;
    }

    if (rEntry.ObjectId.isEmpty())
    {
        aResult.UserMessage = u"无法预览：缺少内容标识"_ustr;
        aResult.Summary = u"preview-failed reason=missing-object-id"_ustr;
        return aResult;
    }

    if (!IsSupportedPreviewTarget(aResult.Target))
    {
        aResult.UserMessage = u"无法预览：不支持的打开目标 "_ustr + aResult.Target;
        aResult.Summary = u"preview-failed reason=unsupported-target target="_ustr
                          + aResult.Target;
        return aResult;
    }

    aResult.Success = true;

    // Chinese user-facing one-liners (Wave D5).
    switch (aResult.FileKind)
    {
        case AIChatPreviewFileKind::Markdown:
            aResult.UserMessage = u"Markdown 将在侧栏详情中只读预览"_ustr;
            break;
        case AIChatPreviewFileKind::Text:
            aResult.UserMessage = aResult.FilePath.isEmpty()
                                      ? u"纯文本将在侧栏详情中只读预览"_ustr
                                      : u"文本文件将在侧栏详情中只读预览"_ustr;
            break;
        case AIChatPreviewFileKind::OfficeDocument:
            aResult.UserMessage = u"PDF/DOCX/ODT 等将通过本地应用只读打开预览"_ustr;
            break;
        case AIChatPreviewFileKind::Image:
            aResult.UserMessage = u"图片将通过本地应用打开预览（不上传）"_ustr;
            break;
        case AIChatPreviewFileKind::Unsupported:
            aResult.UserMessage
                = u"暂不支持内嵌预览，仍可点「打开」尝试用本地应用打开"_ustr;
            break;
        case AIChatPreviewFileKind::Metadata:
        default:
            aResult.UserMessage = u"元数据预览（只读，不修改主文档）"_ustr;
            break;
    }

    // Keep machine tokens for harnesses + short Chinese for list rows.
    OUStringBuffer aSummary;
    aSummary.append(u"preview id="_ustr + rEntry.ObjectId + u" type="_ustr + rEntry.Type
                    + u" target="_ustr + aResult.Target + u" mode="_ustr + aResult.Mode
                    + u" file-kind="_ustr + aResult.FileKindLabel + u" "_ustr
                    + aResult.EvidenceBadge
                    + u" redacted=true hash-only=true read-only=true "_ustr
                    + aResult.SourceMetadata + u" · "_ustr + aResult.UserMessage);
    aResult.Summary = aSummary.makeStringAndClear();
    return aResult;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
