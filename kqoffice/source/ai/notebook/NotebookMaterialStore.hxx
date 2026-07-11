/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Local material library for notebook (NotebookLM-style, offline).
 * index: ~/.config/kqoffice/notebook/materials/index.json
 * text:  ~/.config/kqoffice/notebook/materials/<id>.txt  (extracted/snippet)
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_NOTEBOOK_NOTEBOOKMATERIALSTORE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_NOTEBOOK_NOTEBOOKMATERIALSTORE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <utility>
#include <vector>

namespace kqoffice::ai::notebook
{

struct MaterialTagCount
{
    OUString tag;
    sal_Int32 count = 0;
};

struct MaterialLibraryStats
{
    sal_Int32 total = 0;
    sal_Int32 pinned = 0;
    sal_Int32 tagged = 0;
    sal_Int32 withSnippet = 0;
    sal_Int64 totalChars = 0;
    sal_Int32 uniqueTags = 0;
};

struct NotebookMaterial
{
    OUString id;
    OUString title; ///< display name (filename or user title)
    OUString kind; ///< text|markdown|csv|image|pdf|office|url|other
    OUString sourcePath; ///< original system path (may be empty for pasted text)
    OUString mimeOrExt; ///< e.g. txt, pdf, png
    OUString createdIso;
    OUString lastUsedIso; ///< last send/ask/open touch
    OUString noteId; ///< optional linked note id
    OUString snippet; ///< extracted / stored text (capped)
    sal_Int32 charCount = 0;
    sal_Int64 byteSize = 0;
    bool pinned = false; ///< favorite / pin to top
    OUString tags; ///< space-separated tags, e.g. "项目A 周报" (UI shows #tag)
};

struct NotebookMaterialIndexEntry
{
    OUString id;
    OUString title;
    OUString kind;
    OUString sourcePath;
    OUString mimeOrExt;
    OUString createdIso;
    OUString lastUsedIso;
    OUString noteId;
    sal_Int32 charCount = 0;
    sal_Int64 byteSize = 0;
    bool pinned = false;
    OUString tags;
};

class SAL_DLLPUBLIC_EXPORT NotebookMaterialStore
{
public:
    static OUString rootDir();
    static OUString materialsDir();
    static OUString indexPath();

    static std::vector<NotebookMaterialIndexEntry> listMaterials(const OUString& rNoteIdFilter
                                                                 = OUString());
    /// Keyword search over title/source/snippet/tags (case-insensitive).
    /// Space-separated tokens default to AND.
    /// Tag tokens (#foo): exact tag preferred. Use | or OR between tokens for tag OR.
    /// Example: "#项目A #周报" (AND) · "#项目A | #周报" (OR tags).
    static std::vector<NotebookMaterialIndexEntry>
    searchMaterials(const OUString& rQuery, const OUString& rNoteIdFilter = OUString(),
                    sal_Int32 nMax = 50);
    static NotebookMaterial loadMaterial(const OUString& rId);
    static bool removeMaterial(const OUString& rId);

    /// Import a local file: extract text when possible, always store metadata + snippet.
    static NotebookMaterial importFile(const OUString& rSystemPath,
                                       const OUString& rNoteId = OUString());

    /// Import free-form text/paste as a material.
    static NotebookMaterial importText(const OUString& rTitle, const OUString& rBody,
                                       const OUString& rNoteId = OUString());

    /// Attach material id to a note (updates index only).
    static bool linkToNote(const OUString& rMaterialId, const OUString& rNoteId);

    /// Replace stored snippet (e.g. after deeper PDF/office extraction in UI layer).
    static bool replaceSnippet(const OUString& rId, const OUString& rSnippet);

    /// Bump lastUsedIso for materials (e.g. after send/ask AI).
    static void touchMaterials(const std::vector<OUString>& rIds);

    /// Toggle or set pin/favorite flag (pinned materials sort first).
    static bool setPinned(const OUString& rId, bool bPinned);
    /// Invert pin for each id; returns count changed.
    static sal_Int32 togglePinned(const std::vector<OUString>& rIds);
    /// Pinned materials only (newest lastUsed first), capped.
    static std::vector<NotebookMaterialIndexEntry> listPinnedMaterials(sal_Int32 nMax = 20);

    /// Set tags string (normalized: strip leading #, space-separated).
    static bool setTags(const OUString& rId, const OUString& rTags);
    /// Append tags to existing (dedupe). Returns false if id missing.
    static bool addTags(const OUString& rId, const OUString& rTags);
    /// Normalize user tag input → "tag1 tag2".
    static OUString normalizeTags(const OUString& rRaw);
    /// Display form "#tag1 #tag2".
    static OUString formatTagsDisplay(const OUString& rTags);
    /// Split normalized tags into tokens.
    static std::vector<OUString> splitTags(const OUString& rTags);
    /// Tag frequency (desc count, then name). Cap nMax.
    static std::vector<MaterialTagCount> listTagCloud(sal_Int32 nMax = 40);
    /// One-line cloud e.g. "#周报×5  #项目A×3".
    static OUString formatTagCloudText(sal_Int32 nMax = 20);
    /// Rename tag across all materials (case-insensitive match). Returns materials touched.
    static sal_Int32 renameTag(const OUString& rOldTag, const OUString& rNewTag);

    /// Aggregate library stats (counts / chars / unique tags).
    static MaterialLibraryStats summarizeLibrary();
    /// One-line stats for status bar.
    static OUString formatLibraryStats();
    /// Day key YYYY-MM-DD from lastUsed (fallback created), empty if none.
    static OUString materialDayKey(const NotebookMaterialIndexEntry& rEntry);

    /// Cross-panel focus: workbench asks notebook to show pinned-only once.
    static void requestFocusPinned();
    /// Returns true once if focus was requested (consumes flag).
    static bool consumeFocusPinned();

    /// Build a compact context block for AI injection (selected materials).
    static OUString formatContextBlock(const std::vector<OUString>& rMaterialIds,
                                       sal_Int32 nMaxChars = 12000);

    /// Lightweight PDF text harvest (no PDFium; string/hex scan). Used as baseline.
    static OUString extractPdfTextLightweight(const OUString& rSystemPath,
                                              sal_Int32 nMaxChars = 100000);

    static OUString newId();
    static OUString detectKind(const OUString& rPathOrExt);
};

} // namespace kqoffice::ai::notebook

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
