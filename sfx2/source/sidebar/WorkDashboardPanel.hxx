/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#pragma once

#include <WorkTelemetryStore.hxx>
#include <sfx2/sidebar/PanelLayout.hxx>
#include <tools/link.hxx>
#include <memory>

namespace weld
{
class Button;
class Entry;
class Label;
class RadioButton;
class TextView;
class Toggleable;
class TreeView;
}

namespace sfx2::sidebar
{
class WorkDashboardPanel final : public PanelLayout
{
public:
    explicit WorkDashboardPanel(weld::Widget* pParent);
    ~WorkDashboardPanel() override;

private:
    DECL_LINK(OnPeriodToggled, weld::Toggleable&, void);
    DECL_LINK(OnRefreshClicked, weld::Button&, void);
    DECL_LINK(OnExportDay, weld::Button&, void);
    DECL_LINK(OnExportWeek, weld::Button&, void);
    DECL_LINK(OnExportMonth, weld::Button&, void);
    DECL_LINK(OnPendant, weld::Button&, void);
    DECL_LINK(OnOpenChart, weld::Button&, void);
    DECL_LINK(OnOpenReports, weld::Button&, void);
    DECL_LINK(OnDesktopShortcuts, weld::Button&, void);
    DECL_LINK(OnSharePath, weld::Button&, void);
    DECL_LINK(OnPackageShare, weld::Button&, void);
    DECL_LINK(OnCopySummary, weld::Button&, void);
    DECL_LINK(OnEmailShare, weld::Button&, void);
    DECL_LINK(OnOpenArchive, weld::Button&, void);
    DECL_LINK(OnCompleteWeek, weld::Button&, void);
    DECL_LINK(OnArchiveHistoryActivated, weld::TreeView&, bool);
    DECL_LINK(OnCopyArchivePath, weld::Button&, void);
    DECL_LINK(OnDeleteArchive, weld::Button&, void);
    DECL_LINK(OnClearArchive, weld::Button&, void);
    DECL_LINK(OnWowChart, weld::Button&, void);
    DECL_LINK(OnOpenPinnedMaterials, weld::Button&, void);
    DECL_LINK(OnExportArchiveCsv, weld::Button&, void);
    DECL_LINK(OnSetWeekGoal, weld::Button&, void);
    DECL_LINK(OnCopyGlance, weld::Button&, void);
    DECL_LINK(OnGlanceToAi, weld::Button&, void);

    void Reload();
    void ReloadArchiveHistory();
    void openArchivePath(const OUString& rPath);
    OUString selectedArchivePath() const;
    void DoExport(const OUString& rPeriod, bool bOpenChart = false);
    void showOnboardingIfNeeded();
    void openSidebarDeck(const OUString& rUno);
    void syncWeekGoalEntry();
    OUString currentPeriodKey() const;
    static OUString formatSummaryText(const kqoffice::ai::workbench::WorkPeriodSummary& s);
    static bool openLocalPath(const OUString& rSysPath);
    static bool openMailto(const OUString& rSubject, const OUString& rBody);
    bool copyTextToClipboard(const OUString& rText);
    void doEmailShare();

    std::unique_ptr<weld::RadioButton> m_xDay;
    std::unique_ptr<weld::RadioButton> m_xMonth;
    std::unique_ptr<weld::RadioButton> m_xYear;
    std::unique_ptr<weld::Button> m_xRefresh;
    std::unique_ptr<weld::Button> m_xExportDay;
    std::unique_ptr<weld::Button> m_xExportWeek;
    std::unique_ptr<weld::Button> m_xExportMonth;
    std::unique_ptr<weld::Button> m_xPendant;
    std::unique_ptr<weld::Button> m_xChart;
    std::unique_ptr<weld::Button> m_xReports;
    std::unique_ptr<weld::Button> m_xDesktop;
    std::unique_ptr<weld::Button> m_xShare;
    std::unique_ptr<weld::Button> m_xPackage;
    std::unique_ptr<weld::Button> m_xCopySummary;
    std::unique_ptr<weld::Button> m_xEmail;
    std::unique_ptr<weld::Button> m_xOpenArchive;
    std::unique_ptr<weld::Button> m_xCompleteWeek;
    std::unique_ptr<weld::Button> m_xCopyArchive;
    std::unique_ptr<weld::Button> m_xDeleteArchive;
    std::unique_ptr<weld::Button> m_xClearArchive;
    std::unique_ptr<weld::Button> m_xExportArchiveCsv;
    std::unique_ptr<weld::Button> m_xWowChart;
    std::unique_ptr<weld::Button> m_xOpenPinned;
    std::unique_ptr<weld::Entry> m_xWeekGoal;
    std::unique_ptr<weld::Button> m_xSetWeekGoal;
    std::unique_ptr<weld::Label> m_xGlance;
    std::unique_ptr<weld::Button> m_xCopyGlance;
    std::unique_ptr<weld::Button> m_xGlanceToAi;
    std::unique_ptr<weld::Label> m_xPeriodLabel;
    std::unique_ptr<weld::TextView> m_xSummary;
    std::unique_ptr<weld::TreeView> m_xArchiveHistory;
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
