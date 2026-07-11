/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Local-first notebook (NotebookLM-style, offline).
 * index: ~/.config/kqoffice/notebook/index.json
 * notes: ~/.config/kqoffice/notebook/notes/<id>.md
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_NOTEBOOK_LOCALNOTEBOOKSTORE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_NOTEBOOK_LOCALNOTEBOOKSTORE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::notebook
{

struct NotebookNote
{
    OUString id;
    OUString title;
    OUString body;
    OUString createdIso;
    OUString updatedIso;
    bool pinned = false;
};

struct NotebookIndexEntry
{
    OUString id;
    OUString title;
    OUString updatedIso;
    bool pinned = false;
};

class SAL_DLLPUBLIC_EXPORT LocalNotebookStore
{
public:
    static OUString rootDir();
    static OUString notesDir();
    static OUString indexPath();

    static std::vector<NotebookIndexEntry> listNotes();
    static NotebookNote loadNote(const OUString& rId);
    static bool saveNote(const NotebookNote& rNote);
    static bool removeNote(const OUString& rId);
    static NotebookNote createNote(const OUString& rTitle = OUString());

    /// Prompt-ready brief of recent notes + material library stats for AI inject.
    static OUString formatAiContextBrief(sal_Int32 nRecentNotes = 5);

    static OUString newId();
};

} // namespace kqoffice::ai::notebook

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
