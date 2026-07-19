/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project.
 *
 * Closed-loop local material reader for AI chat:
 *   - @文件:path / @截图:path → extract text (UTF-8 / PDF harvest / OCR / office ZIP)
 *   - @文件夹:path → inventory + sample extracts from authorized-ish local trees
 *
 * Local-first: no cloud OCR by default. Optional KQOFFICE_AI_OCR_CMD / tesseract.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIMATERIALREADER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CHAT_DOCUMENTAIMATERIALREADER_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::chat
{

enum class MaterialKind : sal_uInt8
{
    Text = 0,
    Markdown,
    Csv,
    Pdf,
    Office,
    Image,
    Folder,
    Other,
};

struct MaterialExtractResult
{
    bool success = false;
    MaterialKind kind = MaterialKind::Other;
    OUString kindLabelZh;
    OUString path;
    OUString title;
    /// Extracted / harvested / OCR text (may be empty with a reason).
    OUString text;
    /// How text was obtained: utf8 | pdf-lightweight | ocr | office-zip | inventory | path-only
    OUString method;
    OUString messageZh;
    sal_Int32 fileCount = 0; ///< for folders
    sal_Int32 extractedCount = 0;
};

/// Local multi-format material reader for AI prompt expansion (closed loop).
class SAL_DLLPUBLIC_EXPORT DocumentAIMaterialReader
{
public:
    /// Detect kind from path extension / directory.
    static MaterialKind detectKind(const OUString& rSystemPath);
    static OUString kindLabelZh(MaterialKind eKind);

    /// Extract text from a single file (or return inventory for directory).
    /// nMaxChars caps returned body (default ~12k).
    static MaterialExtractResult extractPath(const OUString& rSystemPath,
                                             sal_Int32 nMaxChars = 12000);

    /// Scan a folder: file inventory + extract up to nMaxFiles readable items.
    static MaterialExtractResult extractFolder(const OUString& rDirPath,
                                               sal_Int32 nMaxFiles = 24,
                                               sal_Int32 nMaxDepth = 3,
                                               sal_Int32 nMaxChars = 14000);

    /// Expand @文件: / @截图: / @文件夹: mentions in a user prompt into a local
    /// context block + original user text. Returns expanded prompt.
    /// rSummaryZh collects a short Chinese status for the UI (may be empty).
    static OUString expandMentionsInPrompt(const OUString& rPrompt, OUString& rSummaryZh,
                                           sal_Int32 nMaxTotalChars = 28000);

    /// True if token is a supported material mention prefix form.
    static bool isMaterialMentionPrefix(const OUString& rToken);

    /// Collect material paths referenced in prompt (system paths).
    static std::vector<OUString> collectMentionPaths(const OUString& rPrompt);
};

} // namespace kqoffice::ai::chat

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
