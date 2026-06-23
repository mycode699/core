/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * V2 W2 Day-1c — command palette popover (cui/ui/commandpalette.ui).
 */

#include <sal/config.h>
#include <config_folders.h>

#include <commandpalette/CommandPalette.hxx>
#include <commandpalette/CommandPaletteLoader.hxx>
#include <commandpalette/CommandPaletteUi.hxx>
#include <commandpalette/RecentStore.hxx>

#include <dispatch/CommandPaletteDispatcher.hxx>

#include <o3tl/safeint.hxx>
#include <rtl/bootstrap.hxx>
#include <sfx2/viewfrm.hxx>
#include <tools/gen.hxx>
#include <vcl/window.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/weld/Popover.hxx>
#include <vcl/weld/TreeView.hxx>
#include <vcl/weld/weld.hxx>
#include <vcl/weld/weldutils.hxx>

#include <memory>
#include <vector>

namespace cui::commandpalette
{
namespace
{
OUString getUserInstallationUrl()
{
    OUString url(u"${$BRAND_BASE_DIR/" LIBO_ETC_FOLDER
                 "/" SAL_CONFIGFILE("bootstrap") ":UserInstallation}"_ustr);
    rtl::Bootstrap::expandMacros(url);
    return url;
}

class CommandPalettePopover final
{
public:
    CommandPalettePopover(weld::Widget* pParent, const tools::Rectangle& rAnchor, SfxViewFrame& rFrame)
        : m_rFrame(rFrame)
        , m_pAnchor(pParent)
        , m_aAnchor(rAnchor)
        , m_xBuilder(Application::CreateBuilder(pParent, u"cui/ui/commandpalette.ui"_ustr))
        , m_xPopover(m_xBuilder->weld_popover(u"CommandPalette"_ustr))
        , m_xSearch(m_xBuilder->weld_entry(u"search_input"_ustr))
        , m_xResults(m_xBuilder->weld_tree_view(u"results_view"_ustr))
        , m_aUserInstallation(getUserInstallationUrl())
        , m_xHint(m_xBuilder->weld_label(u"hint_label"_ustr))
    {
        std::vector<CommandEntry> corpus = CommandPaletteLoader::buildCorpus(rFrame);
        const std::vector<RecentEntry> recents
            = RecentStore::loadFromUser(m_aUserInstallation);
        RecentStore::applyFrequencies(corpus, recents);
        m_aController.setCorpus(std::move(corpus));
        m_xHint->set_visible(m_aController.corpus().empty());

        m_xSearch->connect_changed(LINK(this, CommandPalettePopover, OnSearchChanged));
        m_xSearch->connect_activate(LINK(this, CommandPalettePopover, OnSearchActivated));
        m_xResults->connect_row_activated(LINK(this, CommandPalettePopover, OnRowActivated));
        m_xPopover->connect_closed(LINK(this, CommandPalettePopover, OnPopoverClosed));

        refreshResults(u""_ustr);
    }

    void show()
    {
        m_xPopover->popup_at_rect(m_pAnchor, m_aAnchor);
        m_xSearch->grab_focus();
    }

    void hide() { m_xPopover->popdown(); }

private:
    DECL_LINK(OnSearchChanged, weld::Entry&, void);
    DECL_LINK(OnSearchActivated, weld::Entry&, bool);
    DECL_LINK(OnRowActivated, weld::TreeView&, bool);
    DECL_LINK(OnPopoverClosed, weld::Popover&, void);

    void refreshResults(const OUString& rQuery)
    {
        m_aLastResults = m_aController.queryToResults(rQuery);
        m_xResults->clear();
        for (std::size_t i = 0; i < m_aLastResults.size(); ++i)
        {
            const CommandEntry& rEntry = *m_aLastResults[i].entry;
            OUString aLabel = rEntry.labelEn.isEmpty() ? rEntry.unoCommand : rEntry.labelEn;
            m_xResults->append(OUString::number(static_cast<sal_Int64>(i)), aLabel);
        }
        if (!m_aLastResults.empty())
            m_xResults->select(0);
        m_xHint->set_visible(m_aController.corpus().empty());
    }

    bool dispatchResultAt(int nRow);

    SfxViewFrame& m_rFrame;
    weld::Widget* m_pAnchor;
    tools::Rectangle m_aAnchor;
    CommandPaletteController m_aController;
    std::vector<ScoredEntry> m_aLastResults;
    std::unique_ptr<weld::Builder> m_xBuilder;
    std::unique_ptr<weld::Popover> m_xPopover;
    std::unique_ptr<weld::Entry> m_xSearch;
    std::unique_ptr<weld::TreeView> m_xResults;
    OUString m_aUserInstallation;
    std::unique_ptr<weld::Label> m_xHint;
};

std::unique_ptr<CommandPalettePopover> g_pActivePopover;

bool CommandPalettePopover::dispatchResultAt(int nRow)
{
    if (nRow < 0 || o3tl::make_unsigned(nRow) >= m_aLastResults.size())
        return false;

    const OUString aUrl = m_aLastResults[o3tl::make_unsigned(nRow)].entry->unoCommand;
    const OUString aUserInst = m_aUserInstallation;
    SfxViewFrame& rFrame = m_rFrame;
    hide();
    g_pActivePopover.reset();
    if (sfx2::CommandPaletteDispatcher::Get().dispatchUrl(rFrame, aUrl))
    {
        RecentStore::recordUse(aUserInst, aUrl);
        return true;
    }
    return false;
}

IMPL_LINK_NOARG(CommandPalettePopover, OnSearchChanged, weld::Entry&, void)
{
    refreshResults(m_xSearch->get_text());
}

IMPL_LINK_NOARG(CommandPalettePopover, OnSearchActivated, weld::Entry&, bool)
{
    if (!m_aLastResults.empty())
    {
        int nRow = m_xResults->get_selected_index();
        if (nRow < 0)
            nRow = 0;
        dispatchResultAt(nRow);
        return true;
    }

    const OUString aPrompt = m_xSearch->get_text().trim();
    if (aPrompt.isEmpty())
        return true;

    SfxViewFrame& rFrame = m_rFrame;
    hide();
    g_pActivePopover.reset();
    sfx2::CommandPaletteDispatcher::Get().dispatchChatFallback(rFrame, aPrompt);
    return true;
}

IMPL_LINK(CommandPalettePopover, OnRowActivated, weld::TreeView&, rView, bool)
{
    dispatchResultAt(rView.get_selected_index());
    return true;
}

IMPL_LINK_NOARG(CommandPalettePopover, OnPopoverClosed, weld::Popover&, void)
{
    g_pActivePopover.reset();
}

struct PaletteHookRegistrar
{
    PaletteHookRegistrar()
    {
        sfx2::CommandPaletteDispatcher::RegisterShowPaletteHook(&ShowCommandPalette);
    }
};
const PaletteHookRegistrar g_aPaletteHookRegistrar;

} // namespace

void ShowCommandPalette(SfxViewFrame& rFrame)
{
    if (g_pActivePopover)
        g_pActivePopover->hide();

    tools::Rectangle aRect(Point(0, 0), rFrame.GetWindow().GetSizePixel());
    weld::Window* pParent = weld::GetPopupParent(rFrame.GetWindow(), aRect);
    g_pActivePopover = std::make_unique<CommandPalettePopover>(pParent, aRect, rFrame);
    g_pActivePopover->show();
}

void HideCommandPalette()
{
    if (g_pActivePopover)
    {
        g_pActivePopover->hide();
        g_pActivePopover.reset();
    }
}

} // namespace cui::commandpalette

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
