/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 M9: document-tools → content registry).
 */

#include "AIChatDocumentToolsContentBridge.hxx"

#include "AIChatContentObjectStore.hxx"
#include "AIChatContentRegistry.hxx"
#include "AIChatSourceProvenance.hxx"

#include <DocumentAIDocumentTools.hxx>

#include <comphelper/hash.hxx>
#include <rtl/ustrbuf.hxx>

#include <vector>

namespace sfx2::sidebar
{
namespace
{
OUString Sha256Hex(const OUString& rText)
{
    const OString sUtf8 = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
    const std::vector<unsigned char> aHash = comphelper::Hash::calculateHash(
        sUtf8.getStr(), sUtf8.getLength(), comphelper::HashType::SHA256);
    return OUString::createFromAscii(comphelper::hashToString(aHash));
}

OUString Clip(const OUString& s, sal_Int32 nMax)
{
    if (nMax <= 0 || s.getLength() <= nMax)
        return s;
    return s.copy(0, nMax) + u"…"_ustr;
}
}

OUString AIChatDocumentToolsContentBridge::MakeToolObjectId(const OUString& rToolName,
                                                            const OUString& rPayloadHash)
{
    return u"dto-"_ustr + Sha256Hex(rToolName + u":"_ustr + rPayloadHash).copy(0, 16);
}

OUString AIChatDocumentToolsContentBridge::MakeToolEvidenceId(const OUString& rObjectId)
{
    return AIChatSourceProvenance::MakeLocalEvidenceId(rObjectId);
}

AIChatDocumentToolsContentBridgeResult
AIChatDocumentToolsContentBridge::RegisterPrepResult(
    const kqoffice::ai::chat::DocumentToolPrepResult& rPrep) const
{
    AIChatDocumentToolsContentBridgeResult out;
    out.MainDocumentMutation = false;

    if (!rPrep.ranTools)
    {
        out.Message = u"document-tools-register-skipped reason=no-tools-ran main-document-mutation=false"_ustr;
        return out;
    }

    AIChatContentRegistry aRegistry;
    AIChatSourceProvenance aProvenance;
    AIChatContentObjectStore aObjects;

    auto registerOne = [&](const OUString& rToolName, const OUString& rType,
                           const OUString& rBodyOrSummary) {
        if (rBodyOrSummary.isEmpty())
            return;
        const OUString sHash = Sha256Hex(rBodyOrSummary);
        const OUString sObjectId = MakeToolObjectId(rToolName, sHash);
        const OUString sEvidence = MakeToolEvidenceId(sObjectId);

        // Large tool outputs become local content objects (reference-only in registry).
        OUString sHashRef = u"sha256:"_ustr + sHash;
        if (aObjects.ShouldMaterializeText(rBodyOrSummary)
            || rBodyOrSummary.getLength() > 400)
        {
            const AIChatMaterializedContent mat = aObjects.MaterializeText(rBodyOrSummary);
            if (!mat.Reference.isEmpty())
                sHashRef = mat.Reference;
        }

        AIChatContentRegistryEntry aEntry;
        aEntry.ObjectId = sObjectId;
        aEntry.Type = rType;
        aEntry.SourceSurface = u"document-tools"_ustr;
        aEntry.State = u"registered"_ustr;
        aEntry.EvidenceId = sEvidence;
        aEntry.HashReference = sHashRef;
        aEntry.OpenTarget = u"sidebar-preview"_ustr;
        // Prefer openable text preview when body was materialized as @artifact.
        aEntry.PreviewMode = sHashRef.startsWith(u"@artifact:"_ustr) ? u"read-only-preview"_ustr
                                                                     : u"metadata-summary"_ustr;
        if (!aRegistry.RegisterObject(aEntry))
            return;

        AIChatSourceProvenanceEntry aSource;
        aSource.SourceId = AIChatSourceProvenance::MakeSourceId(sObjectId);
        aSource.SourceType = rType;
        aSource.CitationId = AIChatSourceProvenance::MakeCitationId(sObjectId);
        aSource.EvidenceId = sEvidence;
        aSource.HashReference = sHashRef;
        aSource.SourceSurface = u"document-tools"_ustr;
        aSource.OpenTarget = u"sidebar-preview"_ustr;
        aSource.SpanReference = u"span:document-tools:"_ustr + rToolName;
        aSource.ReviewId = OUString();
        aProvenance.RegisterSource(aSource);

        out.ObjectIds.push_back(sObjectId);
        ++out.RegisteredCount;
    };

    // Always register a compact context activity summary when tools ran.
    OUStringBuffer aCtx;
    aCtx.append(u"document-tools snapshot="_ustr);
    aCtx.append(rPrep.snapshotHash.isEmpty() ? u"(none)"_ustr : rPrep.snapshotHash);
    aCtx.append(u"\n"_ustr);
    for (const auto& act : rPrep.activities)
    {
        aCtx.append(act.summary);
        aCtx.append(u'\n');
    }
    registerOne(u"get_document_context"_ustr, u"document-tool-context"_ustr,
                Clip(aCtx.makeStringAndClear(), 4000));

    // Register read_blocks payload when present (hash/reference only in registry).
    if (!rPrep.promptInjection.isEmpty())
    {
        registerOne(u"read_blocks"_ustr, u"document-tool-read"_ustr,
                    Clip(rPrep.promptInjection, 24000));
    }

    out.Success = out.RegisteredCount > 0;
    out.MainDocumentMutation = false;
    if (out.Success)
    {
        out.Message = u"document-tools-registered count="_ustr
                      + OUString::number(out.RegisteredCount)
                      + u" main-document-mutation=false public-egress=false"_ustr;
    }
    else
    {
        out.Message
            = u"document-tools-register-failed reason=no-entries main-document-mutation=false"_ustr;
    }
    return out;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
