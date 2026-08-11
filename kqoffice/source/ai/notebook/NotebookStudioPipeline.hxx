/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 可圈笔记 · Studio 成品管线（本地）
 * 材料 → 双声脚本规范化 → 成品包（MD/README/元数据/朗读脚本）
 * 不上传；TTS 依赖本机 say / ffmpeg（可选）。
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_NOTEBOOK_NOTEBOOKSTUDIOPIPELINE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_NOTEBOOK_NOTEBOOKSTUDIOPIPELINE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::notebook
{

struct StudioSpeakTurn
{
    char speaker = 'A'; ///< 'A' or 'B'
    OUString text;
};

struct StudioPackageResult
{
    bool success = false;
    OUString packageDir; ///< system path
    OUString scriptPath;
    OUString readmePath;
    OUString speakScriptPath;
    OUString aiffPath; ///< expected export path (may not exist until shell runs)
    OUString message; ///< zh-CN
    sal_Int32 turnCount = 0;
};

/// Studio production helpers shared by UI / tests (local-only).
class SAL_DLLPUBLIC_EXPORT NotebookStudioPipeline
{
public:
    /// Parse dual-voice script lines (A:/B: / 主播A / 说话人1…).
    /// If no labels, alternate A/B by paragraph for product feel.
    static std::vector<StudioSpeakTurn> parseTurns(const OUString& rScript);

    /// Normalize script: ensure A:/B: prefixes, trim, cap turn length, drop empty.
    static OUString normalizeDualVoiceScript(const OUString& rScript);

    /// High-quality Studio prompt instruction fragments (zh).
    static OUString audioOverviewInstruction();
    static OUString videoOverviewInstruction();
    static OUString studioFinishPackageInstruction();

    /// Default package root: ~/.config/kqoffice/notebook/studio-packages
    static OUString defaultPackageRoot();

    /**
     * Write a local product package (no network):
     *   packageDir/
     *     README.md
     *     script.md          (normalized dual-voice)
     *     metadata.json
     *     sources.txt        (optional index)
     *     speak-dual.sh      (macOS say A/B)
     *     export-aiff.sh     (dual-voice AIFF via say + ffmpeg if present)
     */
    static StudioPackageResult exportPackage(const OUString& rTitle, const OUString& rKind,
                                             const OUString& rScriptBody,
                                             const OUString& rSourcesIndex = OUString(),
                                             const OUString& rPackageRoot = OUString());

    /// Build shell that speaks dual-voice live (macOS). Empty on unsupported platforms.
    static OUString buildLiveSpeakShell(const std::vector<StudioSpeakTurn>& rTurns,
                                        const OUString& rVoiceA = OUString(),
                                        const OUString& rVoiceB = OUString());

    /// Build shell that exports true dual-voice AIFF (per-turn + ffmpeg concat when available).
    static OUString buildDualAiffExportShell(const std::vector<StudioSpeakTurn>& rTurns,
                                             const OUString& rOutAiff,
                                             const OUString& rWorkDir,
                                             const OUString& rVoiceA = OUString(),
                                             const OUString& rVoiceB = OUString());

    /// Quality score 0–100 for dual-voice product readiness (labels, turns, length).
    static sal_Int32 scoreScriptQuality(const OUString& rScript);
    static OUString scoreScriptQualityZh(const OUString& rScript);
};

} // namespace kqoffice::ai::notebook

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
