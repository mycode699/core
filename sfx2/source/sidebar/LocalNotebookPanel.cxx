/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "LocalNotebookPanel.hxx"

#include <LocalNotebookStore.hxx>
#include <NotebookMaterialStore.hxx>
#include <WorkTelemetryStore.hxx>
#include <DocumentAIVoiceInput.hxx>
#include <sidebar/MaterialTextEnrich.hxx>

#include <com/sun/star/system/SystemShellExecute.hpp>
#include <com/sun/star/system/SystemShellExecuteFlags.hpp>
#include <com/sun/star/ui/dialogs/TemplateDescription.hpp>
#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <comphelper/errcode.hxx>
#include <comphelper/processfactory.hxx>
#include <com/sun/star/frame/XDispatch.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/util/URL.hpp>
#include <com/sun/star/util/URLTransformer.hpp>
#include <sfx2/filedlghelper.hxx>
#include <sfx2/viewfrm.hxx>
#include <vcl/svapp.hxx>
#include <vcl/unohelp2.hxx>
#include <vcl/vclenum.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/weld/TextView.hxx>
#include <vcl/weld/TreeView.hxx>
#include <vcl/weld/weld.hxx>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <map>
#include <vector>

using namespace kqoffice::ai::notebook;
using namespace kqoffice::ai::workbench;
using namespace kqoffice::ai::chat;

namespace sfx2::sidebar
{
namespace
{
void queuePromptInject(const OUString& rText)
{
    const char* home = std::getenv("HOME");
    if (!home || !*home || rText.isEmpty())
        return;
    const OUString path
        = OUString::fromUtf8(home) + u"/.config/kqoffice/pending-prompt-inject"_ustr;
    const sal_Int32 slash = path.lastIndexOf(u'/');
    if (slash > 0)
    {
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(path.copy(0, slash), dirUrl)
            == osl::FileBase::E_None)
            osl::Directory::createPath(dirUrl);
    }
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(path, url) != osl::FileBase::E_None)
        return;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return;
    f.setSize(0);
    const OString utf8 = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(utf8.getStr(), utf8.getLength(), n);
    f.close();
}

void openAiAssistantDeck()
{
    SfxViewFrame* pFrame = SfxViewFrame::Current();
    if (!pFrame)
        return;
    try
    {
        css::util::URL aUrl;
        aUrl.Complete = u".uno:SidebarDeck.AIChatDeck"_ustr;
        auto xTrans = css::util::URLTransformer::create(comphelper::getProcessComponentContext());
        if (xTrans.is())
            xTrans->parseStrict(aUrl);
        css::uno::Reference<css::frame::XDispatchProvider> xProv(
            pFrame->GetFrame().GetFrameInterface(), css::uno::UNO_QUERY);
        if (!xProv.is())
            return;
        auto xDisp = xProv->queryDispatch(aUrl, u"_self"_ustr, 0);
        if (xDisp.is())
            xDisp->dispatch(aUrl, {});
    }
    catch (...)
    {
    }
}

OUString kindIcon(const OUString& k)
{
    if (k == u"markdown"_ustr || k == u"text"_ustr)
        return u"📄 "_ustr;
    if (k == u"csv"_ustr)
        return u"📊 "_ustr;
    if (k == u"image"_ustr)
        return u"🖼 "_ustr;
    if (k == u"pdf"_ustr)
        return u"📕 "_ustr;
    if (k == u"office"_ustr)
        return u"📘 "_ustr;
    if (k == u"url"_ustr)
        return u"🔗 "_ustr;
    return u"📎 "_ustr;
}
} // namespace

bool LocalNotebookPanel::openLocalPath(const OUString& rSysPath)
{
    if (rSysPath.isEmpty())
        return false;
    try
    {
        OUString url = rSysPath;
        if (!url.startsWith(u"file:"_ustr))
        {
            if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
                return false;
        }
        css::uno::Reference<css::system::XSystemShellExecute> xExec(
            css::system::SystemShellExecute::create(comphelper::getProcessComponentContext()));
        if (!xExec.is())
            return false;
        xExec->execute(url, OUString(), css::system::SystemShellExecuteFlags::URIS_ONLY);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool LocalNotebookPanel::copyTextToClipboard(const OUString& rText)
{
    if (rText.isEmpty())
        return false;
    try
    {
        weld::Widget* p = m_xMatPreview ? static_cast<weld::Widget*>(m_xMatPreview.get())
                                        : static_cast<weld::Widget*>(m_xStatus.get());
        if (!p)
            return false;
        auto xClip = p->get_clipboard();
        if (!xClip.is())
            return false;
        vcl::unohelper::TextDataObject::CopyStringTo(rText, xClip);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

LocalNotebookPanel::LocalNotebookPanel(weld::Widget* pParent)
    : PanelLayout(pParent, u"LocalNotebookPanel"_ustr, u"sfx/ui/localnotebook.ui"_ustr)
    , m_xSearch(m_xBuilder->weld_entry(u"search_entry"_ustr))
    , m_xList(m_xBuilder->weld_tree_view(u"note_list"_ustr))
    , m_xTitle(m_xBuilder->weld_entry(u"title_entry"_ustr))
    , m_xBody(m_xBuilder->weld_text_view(u"body_view"_ustr))
    , m_xNew(m_xBuilder->weld_button(u"new_btn"_ustr))
    , m_xSave(m_xBuilder->weld_button(u"save_btn"_ustr))
    , m_xDelete(m_xBuilder->weld_button(u"delete_btn"_ustr))
    , m_xVoice(m_xBuilder->weld_button(u"voice_btn"_ustr))
    , m_xSendAi(m_xBuilder->weld_button(u"send_ai_btn"_ustr))
    , m_xMaterials(m_xBuilder->weld_tree_view(u"material_list"_ustr))
    , m_xMatSearch(m_xBuilder->weld_entry(u"mat_search_entry"_ustr))
    , m_xMatPreview(m_xBuilder->weld_text_view(u"material_preview"_ustr))
    , m_xMatAdd(m_xBuilder->weld_button(u"mat_add_btn"_ustr))
    , m_xMatPaste(m_xBuilder->weld_button(u"mat_paste_btn"_ustr))
    , m_xMatAttach(m_xBuilder->weld_button(u"mat_attach_btn"_ustr))
    , m_xMatSend(m_xBuilder->weld_button(u"mat_send_btn"_ustr))
    , m_xMatDel(m_xBuilder->weld_button(u"mat_del_btn"_ustr))
    , m_xMatReextract(m_xBuilder->weld_button(u"mat_reextract_btn"_ustr))
    , m_xMatOpen(m_xBuilder->weld_button(u"mat_open_btn"_ustr))
    , m_xMatFolder(m_xBuilder->weld_button(u"mat_folder_btn"_ustr))
    , m_xMatBatch(m_xBuilder->weld_button(u"mat_batch_btn"_ustr))
    , m_xMatAsk(m_xBuilder->weld_button(u"mat_ask_btn"_ustr))
    , m_xMatSelAll(m_xBuilder->weld_button(u"mat_selall_btn"_ustr))
    , m_xMatCopyPath(m_xBuilder->weld_button(u"mat_copypath_btn"_ustr))
    , m_xMatPin(m_xBuilder->weld_button(u"mat_pin_btn"_ustr))
    , m_xMatPinnedAi(m_xBuilder->weld_button(u"mat_pinned_ai_btn"_ustr))
    , m_xMatTag(m_xBuilder->weld_button(u"mat_tag_btn"_ustr))
    , m_xMatPinnedOnly(m_xBuilder->weld_button(u"mat_pinned_only_btn"_ustr))
    , m_xMatTagCloud(m_xBuilder->weld_button(u"mat_tagcloud_btn"_ustr))
    , m_xMatGroupTags(m_xBuilder->weld_button(u"mat_group_tags_btn"_ustr))
    , m_xMatTagRename(m_xBuilder->weld_button(u"mat_tag_rename_btn"_ustr))
    , m_xMatTimeline(m_xBuilder->weld_button(u"mat_timeline_btn"_ustr))
    , m_xMatStats(m_xBuilder->weld_button(u"mat_stats_btn"_ustr))
    , m_xStatus(m_xBuilder->weld_label(u"status_label"_ustr))
{
    if (m_xList)
    {
        m_xList->set_selection_mode(SelectionMode::Single);
        m_xList->connect_selection_changed(LINK(this, LocalNotebookPanel, OnListChanged));
    }
    if (m_xMaterials)
    {
        // Multi-select: ⌘/Ctrl+click · Shift 范围选；批提取/删除/发 AI 作用于选中项
        m_xMaterials->set_selection_mode(SelectionMode::Multiple);
        m_xMaterials->connect_selection_changed(LINK(this, LocalNotebookPanel, OnMatListChanged));
    }
    if (m_xMatPreview)
        m_xMatPreview->set_editable(false);
    if (m_xSearch)
        m_xSearch->connect_changed(LINK(this, LocalNotebookPanel, OnSearchChanged));
    if (m_xNew)
        m_xNew->connect_clicked(LINK(this, LocalNotebookPanel, OnNewClicked));
    if (m_xSave)
        m_xSave->connect_clicked(LINK(this, LocalNotebookPanel, OnSaveClicked));
    if (m_xDelete)
        m_xDelete->connect_clicked(LINK(this, LocalNotebookPanel, OnDeleteClicked));
    if (m_xVoice)
        m_xVoice->connect_clicked(LINK(this, LocalNotebookPanel, OnVoiceClicked));
    if (m_xSendAi)
        m_xSendAi->connect_clicked(LINK(this, LocalNotebookPanel, OnSendAiClicked));
    if (m_xMatAdd)
        m_xMatAdd->connect_clicked(LINK(this, LocalNotebookPanel, OnMatAdd));
    if (m_xMatPaste)
        m_xMatPaste->connect_clicked(LINK(this, LocalNotebookPanel, OnMatPaste));
    if (m_xMatAttach)
        m_xMatAttach->connect_clicked(LINK(this, LocalNotebookPanel, OnMatAttach));
    if (m_xMatSend)
        m_xMatSend->connect_clicked(LINK(this, LocalNotebookPanel, OnMatSend));
    if (m_xMatDel)
        m_xMatDel->connect_clicked(LINK(this, LocalNotebookPanel, OnMatDel));
    if (m_xMatReextract)
        m_xMatReextract->connect_clicked(LINK(this, LocalNotebookPanel, OnMatReextract));
    if (m_xMatOpen)
        m_xMatOpen->connect_clicked(LINK(this, LocalNotebookPanel, OnMatOpenSource));
    if (m_xMatFolder)
        m_xMatFolder->connect_clicked(LINK(this, LocalNotebookPanel, OnMatFolder));
    if (m_xMatBatch)
        m_xMatBatch->connect_clicked(LINK(this, LocalNotebookPanel, OnMatBatch));
    if (m_xMatSearch)
        m_xMatSearch->connect_changed(LINK(this, LocalNotebookPanel, OnMatSearchChanged));
    if (m_xMatAsk)
        m_xMatAsk->connect_clicked(LINK(this, LocalNotebookPanel, OnMatAskAi));
    if (m_xMatSelAll)
        m_xMatSelAll->connect_clicked(LINK(this, LocalNotebookPanel, OnMatSelectAll));
    if (m_xMatCopyPath)
        m_xMatCopyPath->connect_clicked(LINK(this, LocalNotebookPanel, OnMatCopyPath));
    if (m_xMatPin)
        m_xMatPin->connect_clicked(LINK(this, LocalNotebookPanel, OnMatPin));
    if (m_xMatPinnedAi)
        m_xMatPinnedAi->connect_clicked(LINK(this, LocalNotebookPanel, OnMatPinnedAi));
    if (m_xMatTag)
        m_xMatTag->connect_clicked(LINK(this, LocalNotebookPanel, OnMatTag));
    if (m_xMatPinnedOnly)
        m_xMatPinnedOnly->connect_clicked(LINK(this, LocalNotebookPanel, OnMatPinnedOnly));
    if (m_xMatTagCloud)
        m_xMatTagCloud->connect_clicked(LINK(this, LocalNotebookPanel, OnMatTagCloud));
    if (m_xMatGroupTags)
        m_xMatGroupTags->connect_clicked(LINK(this, LocalNotebookPanel, OnMatGroupTags));
    if (m_xMatTagRename)
        m_xMatTagRename->connect_clicked(LINK(this, LocalNotebookPanel, OnMatTagRename));
    if (m_xMatTimeline)
        m_xMatTimeline->connect_clicked(LINK(this, LocalNotebookPanel, OnMatTimeline));
    if (m_xMatStats)
        m_xMatStats->connect_clicked(LINK(this, LocalNotebookPanel, OnMatStats));

    ReloadList();
    applyFocusPinnedIfRequested();
    ReloadMaterials();
}

LocalNotebookPanel::~LocalNotebookPanel() = default;

bool LocalNotebookPanel::matchesSearch(const OUString& rId, const OUString& rTitle) const
{
    if (!m_xSearch)
        return true;
    const OUString q = m_xSearch->get_text().trim().toAsciiLowerCase();
    if (q.isEmpty())
        return true;
    if (rTitle.toAsciiLowerCase().indexOf(q) >= 0)
        return true;
    const NotebookNote n = LocalNotebookStore::loadNote(rId);
    return n.body.toAsciiLowerCase().indexOf(q) >= 0;
}

OUString LocalNotebookPanel::selectedMaterialId() const
{
    if (!m_xMaterials)
        return OUString();
    return m_xMaterials->get_selected_id();
}

std::vector<OUString> LocalNotebookPanel::selectedMaterialIds() const
{
    std::vector<OUString> ids;
    if (!m_xMaterials)
        return ids;
    m_xMaterials->selected_foreach([&](weld::TreeIter& rIt) {
        const OUString id = m_xMaterials->get_id(rIt);
        if (!id.isEmpty())
            ids.push_back(id);
        return false;
    });
    return ids;
}

void LocalNotebookPanel::applyFocusPinnedIfRequested()
{
    if (NotebookMaterialStore::consumeFocusPinned())
    {
        m_bPinnedOnly = true;
        if (m_xMatPinnedOnly)
            m_xMatPinnedOnly->set_label(u"全部"_ustr);
        if (m_xStatus)
            m_xStatus->set_label(u"已切换为仅置顶材料（来自工作中台）"_ustr);
    }
}

void LocalNotebookPanel::ReloadMaterials()
{
    if (!m_xMaterials)
        return;
    applyFocusPinnedIfRequested();
    // Any normal reload exits tag-cloud pick mode (OnMatTagCloud fills the list itself).
    m_bTagCloudMode = false;
    m_xMaterials->clear();
    const OUString q = m_xMatSearch ? m_xMatSearch->get_text().trim() : OUString();
    // Note filter: current note's materials + unlinked; search hits full library then filter
    std::vector<NotebookMaterialIndexEntry> items;
    if (m_bPinnedOnly && q.isEmpty())
        items = NotebookMaterialStore::listPinnedMaterials(100);
    else if (!q.isEmpty())
        items = NotebookMaterialStore::searchMaterials(q, OUString(), 50);
    else
        items = NotebookMaterialStore::listMaterials();

    auto makeLabel = [&](const NotebookMaterialIndexEntry& e, bool bSearchRank, sal_Int32 shown) {
        OUString label = kindIcon(e.kind) + e.title;
        if (e.pinned)
            label = u"📌 "_ustr + label;
        if (!e.tags.isEmpty())
        {
            label += u" "_ustr;
            label += NotebookMaterialStore::formatTagsDisplay(e.tags);
        }
        if (!e.noteId.isEmpty() && e.noteId != m_sCurrentId && !q.isEmpty())
            label = u"↪ "_ustr + label;
        if (e.charCount > 0)
        {
            label += u" · "_ustr;
            label += OUString::number(e.charCount);
            label += u"字"_ustr;
        }
        if (bSearchRank && shown < 3)
            label = u"★"_ustr + label;
        return label;
    };

    auto passFilters = [&](const NotebookMaterialIndexEntry& e) -> bool {
        if (m_bPinnedOnly && !e.pinned)
            return false;
        if (!m_sCurrentId.isEmpty() && !e.noteId.isEmpty() && e.noteId != m_sCurrentId
            && q.isEmpty() && !m_bPinnedOnly && !m_bGroupByTags)
            return false;
        return true;
    };

    sal_Int32 shown = 0;

    // Timeline: group by day of lastUsed/created (newest days first)
    // mode 1 = all, mode 2 = rolling last 7 days only
    if (m_nTimelineMode > 0 && q.isEmpty() && !m_bGroupByTags)
    {
        const bool bThisWeekOnly = (m_nTimelineMode == 2);
        std::map<OUString, std::vector<NotebookMaterialIndexEntry>, std::greater<OUString>> byDay;
        std::vector<NotebookMaterialIndexEntry> undated;
        for (const auto& e : items)
        {
            if (!passFilters(e))
                continue;
            const OUString day = NotebookMaterialStore::materialDayKey(e);
            if (day.isEmpty())
            {
                if (!bThisWeekOnly)
                    undated.push_back(e);
                continue;
            }
            if (bThisWeekOnly && !WorkTelemetryStore::isDayWithinLastN(day, 7))
                continue;
            byDay[day].push_back(e);
        }
        for (auto& kv : byDay)
        {
            m_xMaterials->append(OUString(),
                                 u"—— "_ustr + kv.first + u" · "_ustr
                                     + OUString::number(static_cast<sal_Int32>(kv.second.size()))
                                     + u" 条 ——"_ustr);
            for (const auto& e : kv.second)
            {
                m_xMaterials->append(e.id, makeLabel(e, false, shown));
                ++shown;
            }
        }
        if (!undated.empty() && !bThisWeekOnly)
        {
            m_xMaterials->append(OUString(), u"—— 无日期 ——"_ustr);
            for (const auto& e : undated)
            {
                m_xMaterials->append(e.id, makeLabel(e, false, shown));
                ++shown;
            }
        }
        if (shown == 0)
        {
            if (bThisWeekOnly)
                m_xMaterials->append(
                    OUString(),
                    u"（近 7 日无材料 · 导入/打开源/发 AI 会更新最近使用日）"_ustr);
            else
                m_xMaterials->append(OUString(), u"（尚无材料）"_ustr);
        }
        if (m_xStatus)
            m_xStatus->set_label((bThisWeekOnly ? u"本周时间线 · "_ustr : u"时间线 · "_ustr)
                                 + OUString::number(shown) + u" 条 · "
                                 + NotebookMaterialStore::formatLibraryStats());
        return;
    }

    // Group by tags when requested (and not searching)
    if (m_bGroupByTags && q.isEmpty())
    {
        const auto cloud = NotebookMaterialStore::listTagCloud(40);
        std::vector<bool> used(items.size(), false);
        for (const auto& tc : cloud)
        {
            bool any = false;
            for (size_t i = 0; i < items.size(); ++i)
            {
                if (used[i] || !passFilters(items[i]))
                    continue;
                bool has = false;
                for (const auto& t : NotebookMaterialStore::splitTags(items[i].tags))
                    if (t.equalsIgnoreAsciiCase(tc.tag))
                    {
                        has = true;
                        break;
                    }
                if (!has)
                    continue;
                if (!any)
                {
                    m_xMaterials->append(OUString(),
                                         u"—— #"_ustr + tc.tag + u" ×"_ustr
                                             + OUString::number(tc.count) + u" ——"_ustr);
                    any = true;
                }
                m_xMaterials->append(items[i].id, makeLabel(items[i], false, shown));
                used[i] = true;
                ++shown;
            }
        }
        // untagged
        bool anyUntagged = false;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (used[i] || !passFilters(items[i]))
                continue;
            if (!items[i].tags.isEmpty())
                continue;
            if (!anyUntagged)
            {
                m_xMaterials->append(OUString(), u"—— 未标签 ——"_ustr);
                anyUntagged = true;
            }
            m_xMaterials->append(items[i].id, makeLabel(items[i], false, shown));
            used[i] = true;
            ++shown;
        }
        if (m_xStatus)
            m_xStatus->set_label(u"按标签分组 · "_ustr + OUString::number(shown) + u" 条 · "
                                 + NotebookMaterialStore::formatLibraryStats());
        return;
    }

    // Default: 置顶 / 最近 headers when browsing
    const bool bGroup = q.isEmpty() && !m_bPinnedOnly && m_nTimelineMode == 0;
    bool bHeaderPinned = false;
    bool bHeaderRecent = false;
    for (const auto& e : items)
    {
        if (!passFilters(e))
            continue;
        if (bGroup)
        {
            if (e.pinned && !bHeaderPinned)
            {
                m_xMaterials->append(OUString(), u"—— 置顶 ——"_ustr);
                bHeaderPinned = true;
            }
            else if (!e.pinned && !bHeaderRecent)
            {
                m_xMaterials->append(OUString(), u"—— 最近 ——"_ustr);
                bHeaderRecent = true;
            }
        }
        m_xMaterials->append(e.id, makeLabel(e, !q.isEmpty(), shown));
        ++shown;
    }
    if (m_xStatus)
    {
        if (m_bPinnedOnly && q.isEmpty())
            m_xStatus->set_label(u"仅置顶 · "_ustr + OUString::number(shown) + u" 条 · "
                                 + NotebookMaterialStore::formatLibraryStats());
        else if (!q.isEmpty())
            m_xStatus->set_label(u"材料搜索「"_ustr + q + u"」· "_ustr + OUString::number(shown)
                                 + u" 条（已按相关度排序）"_ustr);
        else if (shown > 0)
            m_xStatus->set_label(NotebookMaterialStore::formatLibraryStats());
    }
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatSearchChanged, weld::Entry&, void)
{
    m_bTagCloudMode = false;
    ReloadMaterials();
}

void LocalNotebookPanel::sendMaterialsToAi(const std::vector<OUString>& rIds,
                                           const OUString& rStatusHint)
{
    std::vector<OUString> ids = rIds;
    if (ids.size() > 6)
        ids.resize(6);
    if (ids.empty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"无材料可发送"_ustr);
        return;
    }
    OUStringBuffer b;
    b.append(u"【请基于以下本地材料回答问题】\n"_ustr);
    const OUString q = m_xMatSearch ? m_xMatSearch->get_text().trim() : OUString();
    if (!q.isEmpty())
    {
        b.append(u"用户检索词："_ustr);
        b.append(q);
        b.append(u"\n"_ustr);
    }
    if (m_xTitle && !m_xTitle->get_text().trim().isEmpty())
    {
        b.append(u"相关笔记："_ustr);
        b.append(m_xTitle->get_text().trim());
        b.append(u"\n"_ustr);
    }
    b.append(u"\n"_ustr);
    b.append(NotebookMaterialStore::formatContextBlock(ids, 12000));
    b.append(u"\n请用中文简明回答，并标注引用了哪份材料。\n"_ustr);
    queuePromptInject(b.makeStringAndClear());
    NotebookMaterialStore::touchMaterials(ids);
    ReloadMaterials();
    openAiAssistantDeck();
    if (m_xStatus)
    {
        if (!rStatusHint.isEmpty())
            m_xStatus->set_label(rStatusHint);
        else
            m_xStatus->set_label(u"已发送 "_ustr
                                 + OUString::number(static_cast<sal_Int32>(ids.size()))
                                 + u" 条材料到 AI 助手"_ustr);
    }
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatAskAi, weld::Button&, void)
{
    // Prefer selection; else current search results; else visible materials
    std::vector<OUString> ids = selectedMaterialIds();
    if (ids.empty() && m_xMaterials)
    {
        const int n = m_xMaterials->n_children();
        for (int i = 0; i < n && static_cast<sal_Int32>(ids.size()) < 6; ++i)
        {
            const OUString id = m_xMaterials->get_id(i);
            if (!id.isEmpty())
                ids.push_back(id);
        }
    }
    if (ids.empty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"无材料可发送，请先搜索或导入"_ustr);
        return;
    }
    sendMaterialsToAi(ids,
                      u"已发送 "_ustr + OUString::number(static_cast<sal_Int32>(ids.size()))
                          + u" 条材料到 AI（检索增强）— 打开「AI 助手」"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatPinnedAi, weld::Button&, void)
{
    const auto pinned = NotebookMaterialStore::listPinnedMaterials(6);
    std::vector<OUString> ids;
    ids.reserve(pinned.size());
    for (const auto& e : pinned)
        if (!e.id.isEmpty())
            ids.push_back(e.id);
    if (ids.empty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"尚无置顶材料 · 先选材料点「置顶」"_ustr);
        return;
    }
    sendMaterialsToAi(ids,
                      u"已发送 "_ustr + OUString::number(static_cast<sal_Int32>(ids.size()))
                          + u" 条置顶材料到 AI — 打开「AI 助手」"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatSelectAll, weld::Button&, void)
{
    if (!m_xMaterials)
        return;
    m_xMaterials->select_all();
    const auto ids = selectedMaterialIds();
    if (m_xStatus)
        m_xStatus->set_label(u"已全选可见材料 "_ustr
                             + OUString::number(static_cast<sal_Int32>(ids.size())) + u" 条"_ustr);
    if (ids.size() == 1)
        ShowMaterialPreview(ids.front());
    else if (ids.size() > 1)
    {
        // reuse multi-select preview path
        OUStringBuffer b;
        b.append(u"多选 "_ustr);
        b.append(static_cast<sal_Int32>(ids.size()));
        b.append(u" 条（全选）\n"_ustr);
        sal_Int32 n = 0;
        for (const auto& id : ids)
        {
            if (n++ >= 12)
            {
                b.append(u"…\n"_ustr);
                break;
            }
            const NotebookMaterial m = NotebookMaterialStore::loadMaterial(id);
            b.append(u"· "_ustr);
            b.append(m.title);
            b.append(u"\n"_ustr);
        }
        if (m_xMatPreview)
            m_xMatPreview->set_text(b.makeStringAndClear());
    }
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatCopyPath, weld::Button&, void)
{
    const auto ids = selectedMaterialIds();
    if (ids.empty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"请先选择材料"_ustr);
        return;
    }
    OUStringBuffer b;
    sal_Int32 n = 0;
    for (const auto& id : ids)
    {
        const NotebookMaterial m = NotebookMaterialStore::loadMaterial(id);
        if (m.sourcePath.isEmpty())
            continue;
        if (n++)
            b.append(u'\n');
        b.append(m.sourcePath);
    }
    const OUString text = b.makeStringAndClear();
    if (text.isEmpty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"选中材料无外部源路径（可能是粘贴文本）"_ustr);
        return;
    }
    if (copyTextToClipboard(text))
    {
        if (m_xStatus)
            m_xStatus->set_label(u"已复制 "_ustr + OUString::number(n) + u" 条路径到剪贴板"_ustr);
    }
    else if (m_xStatus)
        m_xStatus->set_label(u"复制失败"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatPin, weld::Button&, void)
{
    const auto ids = selectedMaterialIds();
    if (ids.empty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"请先选择材料再置顶/取消"_ustr);
        return;
    }
    const sal_Int32 n = NotebookMaterialStore::togglePinned(ids);
    ReloadMaterials();
    // reselect first if possible
    if (!ids.empty() && m_xMaterials)
        m_xMaterials->select_id(ids.front());
    if (ids.size() == 1)
        ShowMaterialPreview(ids.front());
    if (m_xStatus)
        m_xStatus->set_label(u"已切换置顶 "_ustr + OUString::number(n) + u" 条材料"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatTag, weld::Button&, void)
{
    const auto ids = selectedMaterialIds();
    if (ids.empty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"请先选择材料，并在搜索框输入标签（如 项目A 周报）"_ustr);
        return;
    }
    const OUString raw = m_xMatSearch ? m_xMatSearch->get_text().trim() : OUString();
    if (raw.isEmpty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"请在搜索框输入标签再点「打标签」"_ustr);
        return;
    }
    const OUString norm = NotebookMaterialStore::normalizeTags(raw);
    if (norm.isEmpty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"标签无效"_ustr);
        return;
    }
    sal_Int32 n = 0;
    for (const auto& id : ids)
        if (NotebookMaterialStore::addTags(id, norm))
            ++n;
    ReloadMaterials();
    if (!ids.empty() && m_xMaterials)
        m_xMaterials->select_id(ids.front());
    if (ids.size() == 1)
        ShowMaterialPreview(ids.front());
    if (m_xStatus)
        m_xStatus->set_label(u"已为 "_ustr + OUString::number(n) + u" 条材料添加 "
                             + NotebookMaterialStore::formatTagsDisplay(norm));
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatPinnedOnly, weld::Button&, void)
{
    m_bPinnedOnly = !m_bPinnedOnly;
    if (m_xMatPinnedOnly)
        m_xMatPinnedOnly->set_label(m_bPinnedOnly ? u"全部"_ustr : u"仅置顶"_ustr);
    ReloadMaterials();
    if (m_xStatus && !m_bPinnedOnly)
        m_xStatus->set_label(u"已显示全部材料"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatTagCloud, weld::Button&, void)
{
    if (!m_xMaterials)
        return;
    const auto cloud = NotebookMaterialStore::listTagCloud(40);
    // Fill list without going through ReloadMaterials (which clears tag-cloud mode)
    m_xMaterials->clear();
    m_bTagCloudMode = true;
    if (cloud.empty())
    {
        m_xMaterials->append(OUString(), u"（尚无标签）"_ustr);
    }
    else
    {
        m_xMaterials->append(OUString(), u"—— 点选标签筛选（#标签）——"_ustr);
        for (const auto& tc : cloud)
        {
            const OUString id = u"__tag__:"_ustr + tc.tag;
            const OUString label
                = u"#"_ustr + tc.tag + u"  ×"_ustr + OUString::number(tc.count);
            m_xMaterials->append(id, label);
        }
    }
    const OUString cloudText = NotebookMaterialStore::formatTagCloudText(30);
    if (m_xMatPreview)
        m_xMatPreview->set_text(
            u"—— 标签云 ——\n"_ustr + cloudText
            + u"\n\n点选上方标签 → 写入搜索框并筛选\n"
              u"多标签：#a #b 为「与」；#a | #b 为「或」\n"
              u"「标签改名」格式：旧>新"_ustr);
    if (m_xStatus)
        m_xStatus->set_label(u"标签云 · 点选标签筛选 · 共 "_ustr
                             + OUString::number(static_cast<sal_Int32>(cloud.size())) + u" 个"_ustr);
    copyTextToClipboard(cloudText);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatGroupTags, weld::Button&, void)
{
    m_bGroupByTags = !m_bGroupByTags;
    if (m_bGroupByTags)
        m_nTimelineMode = 0;
    if (m_xMatGroupTags)
        m_xMatGroupTags->set_label(m_bGroupByTags ? u"默认序"_ustr : u"按标签"_ustr);
    if (m_xMatTimeline && m_bGroupByTags)
        m_xMatTimeline->set_label(u"时间线"_ustr);
    ReloadMaterials();
    if (m_xStatus)
        m_xStatus->set_label(m_bGroupByTags ? u"已按标签分组"_ustr : u"已恢复默认排序"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatTimeline, weld::Button&, void)
{
    // Cycle: off → all days → this week → off
    m_nTimelineMode = (m_nTimelineMode + 1) % 3;
    if (m_nTimelineMode > 0)
        m_bGroupByTags = false;
    if (m_xMatTimeline)
    {
        if (m_nTimelineMode == 0)
            m_xMatTimeline->set_label(u"时间线"_ustr);
        else if (m_nTimelineMode == 1)
            m_xMatTimeline->set_label(u"本周线"_ustr);
        else
            m_xMatTimeline->set_label(u"默认序"_ustr);
    }
    if (m_xMatGroupTags && m_nTimelineMode > 0)
        m_xMatGroupTags->set_label(u"按标签"_ustr);
    ReloadMaterials();
    if (m_xStatus)
    {
        if (m_nTimelineMode == 0)
            m_xStatus->set_label(u"已恢复默认排序"_ustr);
        else if (m_nTimelineMode == 1)
            m_xStatus->set_label(u"时间线：全部日期（再点切到本周）"_ustr);
        else
            m_xStatus->set_label(u"时间线：仅近 7 日材料"_ustr);
    }
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatStats, weld::Button&, void)
{
    const OUString stats = NotebookMaterialStore::formatLibraryStats();
    const OUString cloud = NotebookMaterialStore::formatTagCloudText(12);
    if (m_xMatPreview)
        m_xMatPreview->set_text(u"—— 材料库统计 ——\n"_ustr + stats + u"\n\n标签云：\n"_ustr
                                + cloud);
    if (m_xStatus)
        m_xStatus->set_label(stats);
    copyTextToClipboard(stats + u"\n"_ustr + cloud);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatTagRename, weld::Button&, void)
{
    const OUString raw = m_xMatSearch ? m_xMatSearch->get_text().trim() : OUString();
    if (raw.isEmpty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"请在搜索框输入：旧标签>新标签（新标签空=删除）"_ustr);
        return;
    }
    // separators: > → = ->
    OUString left, right;
    sal_Int32 sep = raw.indexOf(u'>');
    if (sep < 0)
        sep = raw.indexOf(u'=');
    if (sep < 0)
        sep = raw.indexOf(u"->"_ustr);
    if (sep < 0)
    {
        if (m_xStatus)
            m_xStatus->set_label(u"格式：旧标签>新标签  或  旧=新  或  旧->新"_ustr);
        return;
    }
    if (raw.indexOf(u"->"_ustr) == sep)
    {
        left = raw.copy(0, sep).trim();
        right = raw.copy(sep + 2).trim();
    }
    else
    {
        left = raw.copy(0, sep).trim();
        right = raw.copy(sep + 1).trim();
    }
    const sal_Int32 n = NotebookMaterialStore::renameTag(left, right);
    ReloadMaterials();
    if (m_xStatus)
    {
        if (n == 0)
            m_xStatus->set_label(u"未改动（标签不存在或新旧相同）"_ustr);
        else if (right.isEmpty())
            m_xStatus->set_label(u"已从 "_ustr + OUString::number(n) + u" 条材料移除标签 "
                                 + NotebookMaterialStore::formatTagsDisplay(left));
        else
            m_xStatus->set_label(u"已在 "_ustr + OUString::number(n) + u" 条材料将 "
                                 + NotebookMaterialStore::formatTagsDisplay(left) + u" → "
                                 + NotebookMaterialStore::formatTagsDisplay(right));
    }
}

void LocalNotebookPanel::ReloadList(const OUString& rSelectId)
{
    if (!m_xList)
        return;
    m_xList->clear();
    const auto items = LocalNotebookStore::listNotes();
    int sel = 0;
    int shown = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& e = items[i];
        if (!matchesSearch(e.id, e.title))
            continue;
        OUString label = e.pinned ? u"📌 "_ustr + e.title : e.title;
        m_xList->append(e.id, label);
        if (!rSelectId.isEmpty() && e.id == rSelectId)
            sel = shown;
        ++shown;
    }
    if (m_xList->n_children() > 0)
    {
        m_xList->select(sel);
        LoadSelected();
    }
    else
    {
        m_sCurrentId.clear();
        if (m_xTitle)
            m_xTitle->set_text(OUString());
        if (m_xBody)
            m_xBody->set_text(OUString());
        if (m_xStatus && m_xSearch && !m_xSearch->get_text().trim().isEmpty())
            m_xStatus->set_label(u"无匹配笔记"_ustr);
        ReloadMaterials();
    }
}

void LocalNotebookPanel::LoadSelected()
{
    if (!m_xList)
        return;
    m_sCurrentId = m_xList->get_selected_id();
    if (m_sCurrentId.isEmpty())
        return;
    const NotebookNote n = LocalNotebookStore::loadNote(m_sCurrentId);
    if (m_xTitle)
        m_xTitle->set_text(n.title);
    if (m_xBody)
        m_xBody->set_text(n.body);
    if (m_xStatus)
        m_xStatus->set_label(u"已加载 · "_ustr + n.updatedIso);
    ReloadMaterials();
}

void LocalNotebookPanel::SaveCurrent()
{
    if (m_sCurrentId.isEmpty())
        return;
    NotebookNote n;
    n.id = m_sCurrentId;
    n.title = m_xTitle ? m_xTitle->get_text().trim() : OUString();
    n.body = m_xBody ? m_xBody->get_text() : OUString();
    if (LocalNotebookStore::saveNote(n))
    {
        WorkTelemetryStore::recordSimple(u"notebook_note"_ustr, 1);
        if (m_xStatus)
            m_xStatus->set_label(u"已保存（本地）"_ustr);
        ReloadList(m_sCurrentId);
    }
    else if (m_xStatus)
        m_xStatus->set_label(u"保存失败"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnListChanged, weld::TreeView&, void) { LoadSelected(); }

IMPL_LINK_NOARG(LocalNotebookPanel, OnSearchChanged, weld::Entry&, void)
{
    ReloadList(m_sCurrentId);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnNewClicked, weld::Button&, void)
{
    const NotebookNote n = LocalNotebookStore::createNote(u"未命名笔记"_ustr);
    WorkTelemetryStore::recordSimple(u"notebook_note"_ustr, 1);
    ReloadList(n.id);
    if (m_xStatus)
        m_xStatus->set_label(u"已新建笔记"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnSaveClicked, weld::Button&, void) { SaveCurrent(); }

IMPL_LINK_NOARG(LocalNotebookPanel, OnDeleteClicked, weld::Button&, void)
{
    if (m_sCurrentId.isEmpty())
        return;
    LocalNotebookStore::removeNote(m_sCurrentId);
    m_sCurrentId.clear();
    ReloadList();
    if (m_xStatus)
        m_xStatus->set_label(u"已删除"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnVoiceClicked, weld::Button&, void)
{
    const auto cap = DocumentAIVoiceInput::togglePushToTalk();
    if (m_xStatus)
        m_xStatus->set_label(cap.message);
    if (cap.success && !cap.text.isEmpty() && m_xBody)
    {
        OUString body = m_xBody->get_text();
        if (!body.isEmpty() && !body.endsWith(u"\n"))
            body += u"\n"_ustr;
        body += cap.text;
        m_xBody->set_text(body);
        WorkTelemetryStore::recordSimple(u"voice"_ustr, 1);
    }
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnSendAiClicked, weld::Button&, void)
{
    SaveCurrent();
    OUStringBuffer b;
    b.append(u"【来自本地记事本】"_ustr);
    if (m_xTitle)
    {
        b.append(u"\n标题："_ustr);
        b.append(m_xTitle->get_text());
    }
    b.append(u"\n\n"_ustr);
    if (m_xBody)
        b.append(m_xBody->get_text());

    // Include materials linked to this note (or all visible)
    std::vector<OUString> mats;
    const auto items = NotebookMaterialStore::listMaterials(m_sCurrentId);
    for (const auto& e : items)
    {
        if (e.noteId.isEmpty() || e.noteId == m_sCurrentId)
            mats.push_back(e.id);
        if (mats.size() >= 6)
            break;
    }
    if (!mats.empty())
    {
        b.append(u"\n\n"_ustr);
        b.append(NotebookMaterialStore::formatContextBlock(mats, 8000));
        NotebookMaterialStore::touchMaterials(mats);
    }

    queuePromptInject(b.makeStringAndClear());
    openAiAssistantDeck();
    if (m_xStatus)
        m_xStatus->set_label(u"已发送笔记+材料到 AI 助手"_ustr);
}

void LocalNotebookPanel::EnrichImportedMaterial(NotebookMaterial& rMat)
{
    if (rMat.id.isEmpty() || rMat.sourcePath.isEmpty())
        return;
    if (rMat.kind != u"pdf"_ustr && rMat.kind != u"office"_ustr && rMat.kind != u"image"_ustr)
        return;
    if (m_xStatus)
        m_xStatus->set_label(u"正在本地提取/OCR…"_ustr);
    const OUString deep = EnrichMaterialText(rMat.sourcePath, rMat.kind);
    // Prefer deeper extract when it clearly beats baseline snippet length
    // Images: any OCR with >= 2 chars wins over path-only stub.
    const sal_Int32 threshold
        = (rMat.kind == u"image"_ustr) ? 2 : std::max<sal_Int32>(80, rMat.charCount + 20);
    if (deep.getLength() > threshold)
    {
        if (NotebookMaterialStore::replaceSnippet(rMat.id, deep))
        {
            rMat.snippet = deep;
            rMat.charCount = deep.getLength();
        }
    }
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatAdd, weld::Button&, void)
{
    try
    {
        sfx2::FileDialogHelper aDlg(css::ui::dialogs::TemplateDescription::FILEOPEN_SIMPLE,
                                    FileDialogFlags::NONE, GetFrameWeld());
        aDlg.SetTitle(u"添加本地材料"_ustr);
        aDlg.AddFilter(u"文本与数据"_ustr, u"*.txt;*.md;*.markdown;*.csv;*.tsv;*.json;*.log;*.xml"_ustr);
        aDlg.AddFilter(u"图片"_ustr, u"*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp"_ustr);
        aDlg.AddFilter(u"PDF"_ustr, u"*.pdf"_ustr);
        aDlg.AddFilter(u"办公文稿"_ustr,
                       u"*.odt;*.ods;*.odp;*.docx;*.xlsx;*.pptx;*.rtf;*.doc;*.xls;*.ppt"_ustr);
        aDlg.AddFilter(u"所有文件"_ustr, u"*.*"_ustr);
        if (aDlg.Execute() != ERRCODE_NONE)
            return;
        OUString path = aDlg.GetPath();
        // FileDialog may return URL
        if (path.startsWith(u"file:"_ustr))
        {
            OUString sys;
            if (osl::FileBase::getSystemPathFromFileURL(path, sys) == osl::FileBase::E_None)
                path = sys;
        }
        NotebookMaterial m = NotebookMaterialStore::importFile(path, m_sCurrentId);
        if (m.id.isEmpty())
        {
            if (m_xStatus)
                m_xStatus->set_label(u"导入失败"_ustr);
            return;
        }
        EnrichImportedMaterial(m);
        WorkTelemetryStore::recordSimple(u"notebook_note"_ustr, 1);
        ReloadMaterials();
        if (m_xMaterials)
            m_xMaterials->select_id(m.id);
        ShowMaterialPreview(m.id);
        if (m_xStatus)
            m_xStatus->set_label(u"已导入材料："_ustr + m.title + u" · "_ustr + m.kind + u" · "
                                 + OUString::number(m.charCount) + u" 字"_ustr);
    }
    catch (...)
    {
        if (m_xStatus)
            m_xStatus->set_label(u"文件选择失败"_ustr);
    }
}

void LocalNotebookPanel::ShowMaterialPreview(const OUString& rMaterialId)
{
    if (rMaterialId.isEmpty())
    {
        if (m_xMatPreview)
            m_xMatPreview->set_text(OUString());
        return;
    }
    const NotebookMaterial m = NotebookMaterialStore::loadMaterial(rMaterialId);
    OUStringBuffer b;
    b.append(u"标题："_ustr);
    b.append(m.title);
    b.append(u"\n类型："_ustr);
    b.append(m.kind);
    if (!m.mimeOrExt.isEmpty())
    {
        b.append(u" · "_ustr);
        b.append(m.mimeOrExt);
    }
    b.append(u"\n字数："_ustr);
    b.append(m.charCount);
    if (!m.sourcePath.isEmpty())
    {
        b.append(u"\n源文件："_ustr);
        b.append(m.sourcePath);
    }
    if (!m.createdIso.isEmpty())
    {
        b.append(u"\n导入："_ustr);
        b.append(m.createdIso);
    }
    if (!m.lastUsedIso.isEmpty())
    {
        b.append(u"\n最近使用："_ustr);
        b.append(m.lastUsedIso);
    }
    b.append(u"\n置顶："_ustr);
    b.append(m.pinned ? u"是 📌"_ustr : u"否"_ustr);
    if (!m.tags.isEmpty())
    {
        b.append(u"\n标签："_ustr);
        b.append(NotebookMaterialStore::formatTagsDisplay(m.tags));
    }
    b.append(u"\n\n—— 提取正文 ——\n"_ustr);
    if (m.snippet.isEmpty())
        b.append(u"（无提取文本；可点「重提取」）"_ustr);
    else
    {
        OUString snip = m.snippet;
        if (snip.getLength() > 4000)
            snip = snip.copy(0, 4000) + u"\n…(预览截断)"_ustr;
        b.append(snip);
    }
    if (m_xMatPreview)
        m_xMatPreview->set_text(b.makeStringAndClear());
    if (m_xStatus)
        m_xStatus->set_label(u"材料："_ustr + m.title + u" · "_ustr + m.kind + u" · "
                             + OUString::number(m.charCount) + u" 字"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatListChanged, weld::TreeView&, void)
{
    // Tag cloud click → filter by #tag
    if (m_bTagCloudMode)
    {
        const OUString id = selectedMaterialId();
        if (id.startsWith(u"__tag__:"_ustr))
        {
            const OUString tag = id.copy(8); // after __tag__:
            m_bTagCloudMode = false;
            if (m_xMatSearch)
                m_xMatSearch->set_text(u"#"_ustr + tag);
            ReloadMaterials();
            if (m_xStatus)
                m_xStatus->set_label(u"已筛选标签 #"_ustr + tag);
            return;
        }
        if (m_xStatus)
            m_xStatus->set_label(u"点选标签以筛选 · 或改搜索框退出标签云"_ustr);
        return;
    }

    const auto ids = selectedMaterialIds();
    if (ids.size() > 1)
    {
        if (m_xStatus)
            m_xStatus->set_label(u"已选 "_ustr + OUString::number(static_cast<sal_Int32>(ids.size()))
                                 + u" 条材料 · 批提取/删除/材料→AI 作用于选中项"_ustr);
        if (m_xMatPreview)
        {
            OUStringBuffer b;
            b.append(u"多选 "_ustr);
            b.append(static_cast<sal_Int32>(ids.size()));
            b.append(u" 条：\n"_ustr);
            sal_Int32 n = 0;
            for (const auto& id : ids)
            {
                if (n++ >= 12)
                {
                    b.append(u"…\n"_ustr);
                    break;
                }
                const NotebookMaterial m = NotebookMaterialStore::loadMaterial(id);
                b.append(u"· "_ustr);
                b.append(m.title);
                b.append(u" ("_ustr);
                b.append(m.kind);
                b.append(u", "_ustr);
                b.append(m.charCount);
                b.append(u"字)\n"_ustr);
            }
            m_xMatPreview->set_text(b.makeStringAndClear());
        }
        return;
    }
    ShowMaterialPreview(selectedMaterialId());
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatReextract, weld::Button&, void)
{
    auto ids = selectedMaterialIds();
    if (ids.empty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"请先选择材料"_ustr);
        return;
    }
    if (m_xStatus)
        m_xStatus->set_label(u"正在重提取…"_ustr);
    sal_Int32 ok = 0;
    OUString lastId;
    for (const auto& mid : ids)
    {
        NotebookMaterial m = NotebookMaterialStore::loadMaterial(mid);
        if (m.sourcePath.isEmpty())
            continue;
        if (m.kind != u"pdf"_ustr && m.kind != u"office"_ustr && m.kind != u"image"_ustr)
            continue;
        const OUString deep = EnrichMaterialText(m.sourcePath, m.kind);
        if (deep.getLength() >= 2)
        {
            NotebookMaterialStore::replaceSnippet(m.id, deep);
            ++ok;
            lastId = mid;
        }
    }
    ReloadMaterials();
    if (!lastId.isEmpty() && m_xMaterials)
        m_xMaterials->select_id(lastId);
    if (m_xStatus)
        m_xStatus->set_label(u"重提取完成：成功 "_ustr + OUString::number(ok) + u" / "
                             + OUString::number(static_cast<sal_Int32>(ids.size())));
    if (ids.size() == 1)
        ShowMaterialPreview(ids.front());
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatOpenSource, weld::Button&, void)
{
    const OUString mid = selectedMaterialId();
    if (mid.isEmpty())
        return;
    const NotebookMaterial m = NotebookMaterialStore::loadMaterial(mid);
    if (m.sourcePath.isEmpty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"该材料无外部源文件（粘贴文本）"_ustr);
        return;
    }
    if (openLocalPath(m.sourcePath))
    {
        NotebookMaterialStore::touchMaterials({ mid });
        if (m_xStatus)
            m_xStatus->set_label(u"已打开源文件"_ustr);
        // Refresh list order (MRU) without clearing selection if possible
        ReloadMaterials();
        if (m_xMaterials)
            m_xMaterials->select_id(mid);
    }
    else if (m_xStatus)
        m_xStatus->set_label(u"无法打开："_ustr + m.sourcePath);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatFolder, weld::Button&, void)
{
    const OUString dir = NotebookMaterialStore::materialsDir();
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(dir, url) == osl::FileBase::E_None)
        osl::Directory::createPath(url);
    if (openLocalPath(dir))
    {
        if (m_xStatus)
            m_xStatus->set_label(u"已打开材料夹："_ustr + dir);
    }
    else if (m_xStatus)
        m_xStatus->set_label(u"无法打开材料夹"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatBatch, weld::Button&, void)
{
    // Prefer multi-selection; else all visible materials (max 8).
    std::vector<OUString> targets = selectedMaterialIds();
    if (targets.empty())
    {
        const auto items = NotebookMaterialStore::listMaterials(m_sCurrentId);
        for (const auto& e : items)
        {
            if (targets.size() >= 8)
                break;
            if (!m_sCurrentId.isEmpty() && !e.noteId.isEmpty() && e.noteId != m_sCurrentId)
                continue;
            if (e.kind != u"pdf"_ustr && e.kind != u"office"_ustr && e.kind != u"image"_ustr)
                continue;
            targets.push_back(e.id);
        }
    }
    sal_Int32 done = 0;
    sal_Int32 ok = 0;
    if (m_xStatus)
        m_xStatus->set_label(u"批量重提取中…"_ustr);
    for (const auto& id : targets)
    {
        if (done >= 8)
            break;
        NotebookMaterial m = NotebookMaterialStore::loadMaterial(id);
        if (m.sourcePath.isEmpty())
            continue;
        if (m.kind != u"pdf"_ustr && m.kind != u"office"_ustr && m.kind != u"image"_ustr)
            continue;
        ++done;
        const OUString deep = EnrichMaterialText(m.sourcePath, m.kind);
        if (deep.getLength() >= 2)
        {
            NotebookMaterialStore::replaceSnippet(m.id, deep);
            ++ok;
        }
    }
    ReloadMaterials();
    if (m_xStatus)
    {
        OUStringBuffer b;
        b.append(u"批提取完成：成功 "_ustr);
        b.append(ok);
        b.append(u" / 尝试 "_ustr);
        b.append(done);
        m_xStatus->set_label(b.makeStringAndClear());
    }
    const OUString mid = selectedMaterialId();
    if (!mid.isEmpty())
        ShowMaterialPreview(mid);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatPaste, weld::Button&, void)
{
    const OUString body = m_xBody ? m_xBody->get_text() : OUString();
    if (body.trim().isEmpty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"正文为空，无法存为材料"_ustr);
        return;
    }
    const OUString title = m_xTitle ? m_xTitle->get_text().trim() : OUString();
    const NotebookMaterial m
        = NotebookMaterialStore::importText(title.isEmpty() ? u"笔记摘录"_ustr : title, body,
                                            m_sCurrentId);
    if (m.id.isEmpty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"存为材料失败"_ustr);
        return;
    }
    ReloadMaterials();
    if (m_xMaterials)
        m_xMaterials->select_id(m.id);
    if (m_xStatus)
        m_xStatus->set_label(u"已把正文存为材料"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatAttach, weld::Button&, void)
{
    const OUString mid = selectedMaterialId();
    if (mid.isEmpty() || m_sCurrentId.isEmpty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"请选择材料与笔记"_ustr);
        return;
    }
    NotebookMaterialStore::linkToNote(mid, m_sCurrentId);
    NotebookMaterialStore::touchMaterials({ mid });
    const NotebookMaterial m = NotebookMaterialStore::loadMaterial(mid);
    if (m_xBody)
    {
        OUString body = m_xBody->get_text();
        if (!body.isEmpty() && !body.endsWith(u"\n"))
            body += u"\n"_ustr;
        body += u"\n@材料:"_ustr + m.title + u" ("_ustr + m.id + u")\n"_ustr;
        if (!m.snippet.isEmpty())
        {
            OUString snip = m.snippet;
            if (snip.getLength() > 400)
                snip = snip.copy(0, 400) + u"…"_ustr;
            body += snip + u"\n"_ustr;
        }
        m_xBody->set_text(body);
    }
    SaveCurrent();
    ReloadMaterials();
    if (m_xStatus)
        m_xStatus->set_label(u"已附到当前笔记"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatSend, weld::Button&, void)
{
    std::vector<OUString> ids = selectedMaterialIds();
    if (ids.empty())
    {
        const auto items = NotebookMaterialStore::listMaterials(m_sCurrentId);
        for (const auto& e : items)
        {
            ids.push_back(e.id);
            if (ids.size() >= 4)
                break;
        }
    }
    if (ids.size() > 8)
        ids.resize(8);
    if (ids.empty())
    {
        if (m_xStatus)
            m_xStatus->set_label(u"无材料可发送"_ustr);
        return;
    }
    OUStringBuffer b;
    b.append(u"【请基于以下本地材料回答】\n"_ustr);
    b.append(NotebookMaterialStore::formatContextBlock(ids, 10000));
    queuePromptInject(b.makeStringAndClear());
    NotebookMaterialStore::touchMaterials(ids);
    ReloadMaterials();
    openAiAssistantDeck();
    if (m_xStatus)
        m_xStatus->set_label(u"已发送 "_ustr + OUString::number(static_cast<sal_Int32>(ids.size()))
                             + u" 条材料到 AI 助手"_ustr);
}

IMPL_LINK_NOARG(LocalNotebookPanel, OnMatDel, weld::Button&, void)
{
    const auto ids = selectedMaterialIds();
    if (ids.empty())
        return;
    for (const auto& mid : ids)
        NotebookMaterialStore::removeMaterial(mid);
    ReloadMaterials();
    if (m_xMatPreview)
        m_xMatPreview->set_text(OUString());
    if (m_xStatus)
        m_xStatus->set_label(u"已删除 "_ustr + OUString::number(static_cast<sal_Int32>(ids.size()))
                             + u" 条材料"_ustr);
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
