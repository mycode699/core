/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Post-apply vision evidence loop (local PNG only, no upload).
 * Pairs pre/post screenshots + plan ops into an auditable evidence card.
 * Optional light-model *text* commentary (does not send image bytes by default).
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIVISIONEVIDENCE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIVISIONEVIDENCE_HXX

#include <AgentChatDiffExtractor.hxx>
#include <DocumentAIApply.hxx>

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::chat
{

struct VisionFileMeta
{
    bool exists = false;
    OUString path;
    sal_Int64 sizeBytes = 0;
    sal_Int64 mtimeSec = 0;
};

struct VisionEvidenceReport
{
    bool hasPre = false;
    bool hasPost = false;
    VisionFileMeta pre;
    VisionFileMeta post;
    /// soft-ok | soft-warn | skip
    OUString status;
    OUString summaryZh;
    /// Longer transcript card (Markdown-ish plain text).
    OUString cardZh;
    /// Dedicated pre/post comparison checklist card (local only).
    OUString diffCardZh;
    /// Prompt for optional light-model text commentary (no image binary).
    OUString modelCommentPrompt;
    /// Filled by panel after optional light call.
    OUString modelCommentZh;
    sal_Int64 sizeDeltaBytes = 0;
    sal_Int64 mtimeDeltaSec = 0;
    /// Soft fingerprints (size+sample checksum) for pre/post; empty if missing.
    OUString preFingerprint;
    OUString postFingerprint;
    bool fingerprintsMatch = false;
    /// Resolved local vision model at report time (informational).
    OUString resolvedVisionModel;
};

/// Result of optional local multimodal describe (never cloud upload of PNG).
struct LocalVisionDescribeResult
{
    /// ok | skip | error | text-fallback
    OUString status;
    bool usedLocalImages = false;
    /// Always false for public egress; local Ollama loopback only.
    bool publicNetworkAttempted = false;
    OUString contentZh;
    /// ollama-local | vision-cmd | none
    OUString backend;
    OUString modelHint;
};

/// Local vision evidence helpers for approve-apply path.
class SAL_DLLPUBLIC_EXPORT DocumentAIVisionEvidence
{
public:
    static VisionFileMeta statLocalFile(const OUString& rSysPath);

    /// Build report from pre/post paths + apply plan context. Never uploads.
    static VisionEvidenceReport buildReport(const OUString& rPrePath, const OUString& rPostPath,
                                            const ApplyPlan& rPlan,
                                            const DocumentAIApplyResult& rApply,
                                            const OUString& rSurface = OUString());

    /// One-line status for sidebar.
    static OUString formatStatusLine(const VisionEvidenceReport& r);

    /// Prompt for light slot: describe expected visual delta (text only).
    static OUString buildDescribePrompt(const VisionEvidenceReport& r, const ApplyPlan& rPlan);

    /// Prompt when local model *can* see PNG bytes (still no public upload).
    static OUString buildLocalMultimodalPrompt(const VisionEvidenceReport& r,
                                               const ApplyPlan& rPlan);

    /// Read local pre/post PNG and ask local vision (Ollama loopback or visionCmd).
    /// Never sends images to public cloud. Caps file size; soft-fails to empty.
    static LocalVisionDescribeResult describeWithLocalImages(
        const VisionEvidenceReport& r, const ApplyPlan& rPlan,
        const OUString& rVisionModel = OUString(), const OUString& rVisionCmd = OUString());

    /// Resolve vision model via routing snapshot + installed Ollama tags + prefs/env.
    static OUString resolveLocalVisionModel(const OUString& rPreferred = OUString());

    /// Human status for sidebar: resolved model + source chain (no network).
    static OUString formatVisionRouteStatusZh(const OUString& rPreferred = OUString());

    /// Soft local fingerprint (size + first-bytes checksum) — not cryptographic.
    static OUString softFingerprint(const OUString& rSysPath);

    /// Build standalone pre/post diff checklist card (used inside buildReport).
    static OUString buildDiffCard(const VisionEvidenceReport& r, const ApplyPlan& rPlan,
                                  const DocumentAIApplyResult& rApply);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
