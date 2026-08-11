/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 可圈笔记 · multi-notebook project (NotebookLM-style).
 * projects: ~/.config/kqoffice/notebook/projects/index.json
 * each:    ~/.config/kqoffice/notebook/projects/<id>.json
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_NOTEBOOK_NOTEBOOKPROJECTSTORE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_NOTEBOOK_NOTEBOOKPROJECTSTORE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::notebook
{

struct StudioArtifact
{
    OUString id;
    OUString kind; ///< audio-script | video-overview | guide | brief | faq | timeline | flashcards | mindmap
    OUString title;
    OUString body;
    OUString createdIso;
};

struct ChatTurn
{
    OUString role; ///< user | assistant | system
    OUString text;
};

struct NotebookProject
{
    OUString id;
    OUString title;
    OUString createdIso;
    OUString updatedIso;
    std::vector<OUString> materialIds; ///< enabled sources (order = citation index)
    std::vector<ChatTurn> chat;
    std::vector<StudioArtifact> artifacts;
};

struct NotebookProjectIndexEntry
{
    OUString id;
    OUString title;
    OUString updatedIso;
    sal_Int32 sourceCount = 0;
};

class SAL_DLLPUBLIC_EXPORT NotebookProjectStore
{
public:
    static OUString rootDir();
    static OUString projectsDir();
    static OUString indexPath();

    static std::vector<NotebookProjectIndexEntry> listProjects();
    static NotebookProject loadProject(const OUString& rId);
    static bool saveProject(const NotebookProject& rProject);
    static bool removeProject(const OUString& rId);
    static NotebookProject createProject(const OUString& rTitle = OUString());

    /// Active project id (last opened); empty if none.
    static OUString activeProjectId();
    static void setActiveProjectId(const OUString& rId);

    static OUString newId();
};

} // namespace kqoffice::ai::notebook

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
