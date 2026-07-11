/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#pragma once

#include <sfx2/sidebar/PanelLayout.hxx>
#include <tools/link.hxx>
#include <rtl/ustring.hxx>
#include <memory>
#include <vector>

namespace kqoffice::ai::notebook
{
struct NotebookMaterial;
}

namespace weld
{
class Button;
class Entry;
class Label;
class TextView;
class TreeView;
}

namespace sfx2::sidebar
{
class LocalNotebookPanel final : public PanelLayout
{
public:
    explicit LocalNotebookPanel(weld::Widget* pParent);
    ~LocalNotebookPanel() override;

private:
    DECL_LINK(OnNewClicked, weld::Button&, void);
    DECL_LINK(OnSaveClicked, weld::Button&, void);
    DECL_LINK(OnDeleteClicked, weld::Button&, void);
    DECL_LINK(OnVoiceClicked, weld::Button&, void);
    DECL_LINK(OnSendAiClicked, weld::Button&, void);
    DECL_LINK(OnListChanged, weld::TreeView&, void);
    DECL_LINK(OnSearchChanged, weld::Entry&, void);
    DECL_LINK(OnMatAdd, weld::Button&, void);
    DECL_LINK(OnMatPaste, weld::Button&, void);
    DECL_LINK(OnMatAttach, weld::Button&, void);
    DECL_LINK(OnMatSend, weld::Button&, void);
    DECL_LINK(OnMatDel, weld::Button&, void);
    DECL_LINK(OnMatListChanged, weld::TreeView&, void);
    DECL_LINK(OnMatReextract, weld::Button&, void);
    DECL_LINK(OnMatOpenSource, weld::Button&, void);
    DECL_LINK(OnMatFolder, weld::Button&, void);
    DECL_LINK(OnMatBatch, weld::Button&, void);
    DECL_LINK(OnMatSearchChanged, weld::Entry&, void);
    DECL_LINK(OnMatAskAi, weld::Button&, void);
    DECL_LINK(OnMatSelectAll, weld::Button&, void);
    DECL_LINK(OnMatCopyPath, weld::Button&, void);
    DECL_LINK(OnMatPin, weld::Button&, void);
    DECL_LINK(OnMatPinnedAi, weld::Button&, void);
    DECL_LINK(OnMatTag, weld::Button&, void);
    DECL_LINK(OnMatPinnedOnly, weld::Button&, void);
    DECL_LINK(OnMatTagCloud, weld::Button&, void);
    DECL_LINK(OnMatGroupTags, weld::Button&, void);
    DECL_LINK(OnMatTagRename, weld::Button&, void);
    DECL_LINK(OnMatTimeline, weld::Button&, void);
    DECL_LINK(OnMatStats, weld::Button&, void);

    void ReloadList(const OUString& rSelectId = OUString());
    void sendMaterialsToAi(const std::vector<OUString>& rIds, const OUString& rStatusHint);
    void ReloadMaterials();
    void applyFocusPinnedIfRequested();
    void LoadSelected();
    void SaveCurrent();
    void ShowMaterialPreview(const OUString& rMaterialId);
    void EnrichImportedMaterial(kqoffice::ai::notebook::NotebookMaterial& rMat);
    bool matchesSearch(const OUString& rId, const OUString& rTitle) const;
    OUString selectedMaterialId() const;
    std::vector<OUString> selectedMaterialIds() const;
    static bool openLocalPath(const OUString& rSysPath);
    bool copyTextToClipboard(const OUString& rText);

    std::unique_ptr<weld::Entry> m_xSearch;
    std::unique_ptr<weld::TreeView> m_xList;
    std::unique_ptr<weld::Entry> m_xTitle;
    std::unique_ptr<weld::TextView> m_xBody;
    std::unique_ptr<weld::Button> m_xNew;
    std::unique_ptr<weld::Button> m_xSave;
    std::unique_ptr<weld::Button> m_xDelete;
    std::unique_ptr<weld::Button> m_xVoice;
    std::unique_ptr<weld::Button> m_xSendAi;
    std::unique_ptr<weld::TreeView> m_xMaterials;
    std::unique_ptr<weld::Entry> m_xMatSearch;
    std::unique_ptr<weld::TextView> m_xMatPreview;
    std::unique_ptr<weld::Button> m_xMatAdd;
    std::unique_ptr<weld::Button> m_xMatPaste;
    std::unique_ptr<weld::Button> m_xMatAttach;
    std::unique_ptr<weld::Button> m_xMatSend;
    std::unique_ptr<weld::Button> m_xMatDel;
    std::unique_ptr<weld::Button> m_xMatReextract;
    std::unique_ptr<weld::Button> m_xMatOpen;
    std::unique_ptr<weld::Button> m_xMatFolder;
    std::unique_ptr<weld::Button> m_xMatBatch;
    std::unique_ptr<weld::Button> m_xMatAsk;
    std::unique_ptr<weld::Button> m_xMatSelAll;
    std::unique_ptr<weld::Button> m_xMatCopyPath;
    std::unique_ptr<weld::Button> m_xMatPin;
    std::unique_ptr<weld::Button> m_xMatPinnedAi;
    std::unique_ptr<weld::Button> m_xMatTag;
    std::unique_ptr<weld::Button> m_xMatPinnedOnly;
    std::unique_ptr<weld::Button> m_xMatTagCloud;
    std::unique_ptr<weld::Button> m_xMatGroupTags;
    std::unique_ptr<weld::Button> m_xMatTagRename;
    std::unique_ptr<weld::Button> m_xMatTimeline;
    std::unique_ptr<weld::Button> m_xMatStats;
    std::unique_ptr<weld::Label> m_xStatus;
    OUString m_sCurrentId;
    bool m_bPinnedOnly = false;
    bool m_bGroupByTags = false;
    bool m_bTagCloudMode = false; ///< material list shows clickable tag cloud
    /// 0=off, 1=all days timeline, 2=this rolling week only
    sal_Int32 m_nTimelineMode = 0;
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
