/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 可圈笔记 · NotebookLM three-column workspace (Sources | Chat | Studio).
 * Multi-notebook projects, grounded chat, Studio artifacts, URL sources, TTS script.
 */

#include <dispatch/KqNotebookDispatcher.hxx>
#include <startcentertheme.hxx>

#include <AgentChatDiffExtractor.hxx>
#include <DocumentAIApply.hxx>
#include <DocumentAIDocumentTools.hxx>
#include <DocumentAIVoiceInput.hxx>
#include <LocalNotebookStore.hxx>
#include <LocalSpeechHub.hxx>
#include <MediaTranscriptService.hxx>
#include <NotebookStudioPipeline.hxx>
#include <NotebookMaterialStore.hxx>
#include <NotebookProjectStore.hxx>
#include <ProviderStreamHelper.hxx>
#include <ModelRoles.hxx>
#include <WorkTelemetryStore.hxx>

#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XDesktop2.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/uno/Sequence.hxx>
#include <vcl/stdtext.hxx>
#include <comphelper/errcode.hxx>
#include <comphelper/processfactory.hxx>
#include <osl/file.hxx>
#include <osl/process.h>
#include <osl/thread.hxx>
#include <rtl/ustrbuf.hxx>
#include <sfx2/filedlghelper.hxx>
#include <sfx2/viewfrm.hxx>
#include <tools/stream.hxx>
#include <tools/urlobj.hxx>
#include <unotools/ucbstreamhelper.hxx>
#include <vcl/svapp.hxx>
#include <vcl/timer.hxx>
#include <vcl/vclenum.hxx>
#include <vcl/weld/Builder.hxx>
#include <vcl/weld/ComboBox.hxx>
#include <vcl/weld/DialogController.hxx>
#include <vcl/weld/Entry.hxx>
#include <vcl/weld/MessageDialog.hxx>
#include <vcl/weld/TextView.hxx>
#include <vcl/weld/TreeView.hxx>
#include <vcl/weld/Widget.hxx>
#include <vcl/weld/weld.hxx>

#include <com/sun/star/ui/dialogs/TemplateDescription.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
// std::min / system used in OnTxSave / play cue

using namespace kqoffice::ai::notebook;

namespace sfx2
{
namespace
{
OUString fetchUrlText(const OUString& rUrl)
{
    // Basic HTTP(S) fetch via UCB; strip tags lightly for grounding.
    try
    {
        INetURLObject aUrl(rUrl);
        if (aUrl.GetProtocol() != INetProtocol::Http && aUrl.GetProtocol() != INetProtocol::Https)
            return OUString();
        std::unique_ptr<SvStream> pStream = utl::UcbStreamHelper::CreateStream(
            rUrl, StreamMode::STD_READ);
        if (!pStream || pStream->GetError())
            return OUString();
        const sal_uInt64 nSize = pStream->remainingSize();
        if (nSize == 0 || nSize > 2 * 1024 * 1024)
            return OUString();
        std::vector<char> buf(static_cast<size_t>(nSize));
        sal_uInt64 nRead = pStream->ReadBytes(buf.data(), nSize);
        OUString html = OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(nRead)));
        // crude tag strip
        OUStringBuffer plain;
        bool inTag = false;
        sal_Int32 nOut = 0;
        for (sal_Int32 i = 0; i < html.getLength() && nOut < 50000; ++i)
        {
            const sal_Unicode c = html[i];
            if (c == u'<')
            {
                inTag = true;
                continue;
            }
            if (c == u'>')
            {
                inTag = false;
                plain.append(u' ');
                ++nOut;
                continue;
            }
            if (!inTag)
            {
                plain.append(c);
                ++nOut;
            }
        }
        return plain.makeStringAndClear().trim();
    }
    catch (...)
    {
        return OUString();
    }
}

bool writeSysFile(const OUString& rSysPath, const OUString& rContent)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    const OString utf8 = OUStringToOString(rContent, RTL_TEXTENCODING_UTF8);
    sal_uInt64 nw = 0;
    const bool ok = f.write(utf8.getStr(), utf8.getLength(), nw) == osl::FileBase::E_None;
    f.close();
    return ok;
}

OUString shellQuote(const OUString& s)
{
    // single-quote for bash; escape embedded '
    OUStringBuffer b;
    b.append(u'\'');
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        if (s[i] == u'\'')
            b.append(u"'\\''"_ustr);
        else
            b.append(s[i]);
    }
    b.append(u'\'');
    return b.makeStringAndClear();
}

void runDetachedShell(const OUString& rScriptPath)
{
#if defined(MACOSX) || defined(__APPLE__)
    rtl_uString* pArgs[3] = {};
    OUString sh(u"/bin/bash"_ustr);
    OUString arg(rScriptPath);
    pArgs[0] = sh.pData;
    pArgs[1] = arg.pData;
    oslProcess hProc = nullptr;
    osl_executeProcess(sh.pData, pArgs + 1, 1, osl_Process_DETACHED, nullptr, nullptr, nullptr, 0,
                       &hProc);
    if (hProc)
        osl_freeProcessHandle(hProc);
#else
    (void)rScriptPath;
#endif
}

/// Dual-voice sequential TTS via macOS `say` (A/B different voices).
/// Export path: true dual-voice AIFF (per-turn + ffmpeg concat when available).
OUString speakScriptMacDual(const OUString& rScript, bool bExportAiff)
{
#if defined(MACOSX) || defined(__APPLE__)
    using kqoffice::ai::notebook::NotebookStudioPipeline;
    const auto turns = NotebookStudioPipeline::parseTurns(rScript);
    if (turns.empty())
        return u"无可用脚本文本"_ustr;

    const OUString quality = NotebookStudioPipeline::scoreScriptQualityZh(rScript);

    if (bExportAiff)
    {
        const char* home = std::getenv("HOME");
        const OUString downloads = OUString::fromUtf8(home && *home ? home : "/tmp")
                                   + u"/Downloads"_ustr;
        const OUString aiffPath = downloads + u"/可圈笔记-双声音频概览.aiff"_ustr;
        const OUString work = u"/tmp/kq-notebook-aiff-turns"_ustr;
        const OUString shBody
            = NotebookStudioPipeline::buildDualAiffExportShell(turns, aiffPath, work);
        if (shBody.isEmpty())
            return u"无法生成导出脚本"_ustr;
        // Ensure Downloads exists in shell; prepend mkdir
        OUString full = u"#!/bin/bash\nmkdir -p "_ustr + shellQuote(downloads) + u"\n"_ustr
                        + shBody;
        const OUString path = u"/tmp/kq-notebook-export-dual.sh"_ustr;
        writeSysFile(path, full);
        runDetachedShell(path);
        return u"正在导出双声 AIFF（A/B 真交替）· "_ustr + aiffPath + u" · "_ustr + quality;
    }

    const OUString shBody = NotebookStudioPipeline::buildLiveSpeakShell(turns);
    if (shBody.isEmpty())
        return u"无法生成朗读脚本"_ustr;
    const OUString path = u"/tmp/kq-notebook-speak.sh"_ustr;
    writeSysFile(path, shBody);
    runDetachedShell(path);
    return u"正在双声朗读 · "_ustr + quality;
#else
    (void)rScript;
    (void)bExportAiff;
    return u"当前平台未接入系统 TTS"_ustr;
#endif
}

/// Export local Studio product package (script + README + dual speak/export shells).
OUString exportStudioProductPackage(const OUString& rTitle, const OUString& rKind,
                                    const OUString& rBody, const OUString& rSources)
{
    using kqoffice::ai::notebook::NotebookStudioPipeline;
    const auto pkg
        = NotebookStudioPipeline::exportPackage(rTitle, rKind, rBody, rSources);
    if (!pkg.success)
        return pkg.message.isEmpty() ? u"成品包导出失败"_ustr : pkg.message;
#if defined(MACOSX) || defined(__APPLE__)
    // chmod + open package folder
    OUStringBuffer sh;
    sh.append(u"#!/bin/bash\nchmod +x "_ustr);
    sh.append(shellQuote(pkg.speakScriptPath));
    sh.append(u" "_ustr);
    sh.append(shellQuote(pkg.packageDir + u"/export-aiff.sh"_ustr));
    sh.append(u" 2>/dev/null\nopen "_ustr);
    sh.append(shellQuote(pkg.packageDir));
    sh.append(u"\n"_ustr);
    const OUString path = u"/tmp/kq-notebook-open-package.sh"_ustr;
    writeSysFile(path, sh.makeStringAndClear());
    runDetachedShell(path);
#endif
    return pkg.message;
}

class KqNotebookController final : public weld::GenericDialogController
{
public:
    explicit KqNotebookController(weld::Window* pParent)
        : GenericDialogController(pParent, u"sfx/ui/kqnotebook.ui"_ustr, u"KqNotebookDialog"_ustr)
        , m_xNbCombo(m_xBuilder->weld_combo_box(u"nb_combo"_ustr))
        , m_xSourceList(m_xBuilder->weld_tree_view(u"source_list"_ustr))
        , m_xSrcStatus(m_xBuilder->weld_label(u"src_status"_ustr))
        , m_xTranscriptEdit(m_xBuilder->weld_text_view(u"transcript_edit"_ustr))
        , m_xChatView(m_xBuilder->weld_text_view(u"chat_view"_ustr))
        , m_xChatEntry(m_xBuilder->weld_entry(u"chat_entry"_ustr))
        , m_xChatStatus(m_xBuilder->weld_label(u"chat_status"_ustr))
        , m_xStudioView(m_xBuilder->weld_text_view(u"studio_view"_ustr))
        , m_xArtifactList(m_xBuilder->weld_tree_view(u"artifact_list"_ustr))
        , m_xBtnAddFile(m_xBuilder->weld_button(u"btn_add_file"_ustr))
        , m_xBtnAddPaste(m_xBuilder->weld_button(u"btn_add_paste"_ustr))
        , m_xBtnAddUrl(m_xBuilder->weld_button(u"btn_add_url"_ustr))
        , m_xBtnAddTranscript(m_xBuilder->weld_button(u"btn_add_transcript"_ustr))
        , m_xBtnLocalAsr(m_xBuilder->weld_button(u"btn_local_asr"_ustr))
        , m_xBtnSrcAll(m_xBuilder->weld_button(u"btn_src_all"_ustr))
        , m_xBtnSrcPreview(m_xBuilder->weld_button(u"btn_src_preview"_ustr))
        , m_xBtnSrcRemove(m_xBuilder->weld_button(u"btn_src_remove"_ustr))
        , m_xBtnTxLoad(m_xBuilder->weld_button(u"btn_tx_load"_ustr))
        , m_xBtnTxSave(m_xBuilder->weld_button(u"btn_tx_save"_ustr))
        , m_xBtnTxMedia(m_xBuilder->weld_button(u"btn_tx_media"_ustr))
        , m_xBtnTxMp3(m_xBuilder->weld_button(u"btn_tx_mp3"_ustr))
        , m_xBtnTxWav(m_xBuilder->weld_button(u"btn_tx_wav"_ustr))
        , m_xBtnTxChapters(m_xBuilder->weld_button(u"btn_tx_chapters"_ustr))
        , m_xBtnTxAiMinutes(m_xBuilder->weld_button(u"btn_tx_ai_minutes"_ustr))
        , m_xBtnTxAiPolish(m_xBuilder->weld_button(u"btn_tx_ai_polish"_ustr))
        , m_xBtnTxAiTodo(m_xBuilder->weld_button(u"btn_tx_ai_todo"_ustr))
        , m_xBtnTxSpeak(m_xBuilder->weld_button(u"btn_tx_speak"_ustr))
        , m_xBtnTxDiarize(m_xBuilder->weld_button(u"btn_tx_diarize"_ustr))
        , m_xSpkCount(m_xBuilder->weld_combo_box(u"spk_count"_ustr))
        , m_xSpeakerNames(m_xBuilder->weld_entry(u"speaker_names"_ustr))
        , m_xBtnTxRename(m_xBuilder->weld_button(u"btn_tx_rename"_ustr))
        , m_xBtnTxPlayCue(m_xBuilder->weld_button(u"btn_tx_play_cue"_ustr))
        , m_xBtnTxMergeAdj(m_xBuilder->weld_button(u"btn_tx_merge_adj"_ustr))
        , m_xBtnTxMergeNext(m_xBuilder->weld_button(u"btn_tx_merge_next"_ustr))
        , m_xBtnTxCycleSpk(m_xBuilder->weld_button(u"btn_tx_cycle_spk"_ustr))
        , m_xBtnTxDelCue(m_xBuilder->weld_button(u"btn_tx_del_cue"_ustr))
        , m_xBtnTxUp(m_xBuilder->weld_button(u"btn_tx_up"_ustr))
        , m_xBtnTxDown(m_xBuilder->weld_button(u"btn_tx_down"_ustr))
        , m_xTimelineList(m_xBuilder->weld_tree_view(u"timeline_list"_ustr))
        , m_xBtnSend(m_xBuilder->weld_button(u"btn_send"_ustr))
        , m_aLiveTick("KqNotebookLiveStt")
        , m_xBtnVoice(m_xBuilder->weld_button(u"btn_voice"_ustr))
        , m_xBtnMeeting(m_xBuilder->weld_button(u"btn_meeting"_ustr))
        , m_xBtnTtsReply(m_xBuilder->weld_button(u"btn_tts_reply"_ustr))
        , m_xBtnClose(m_xBuilder->weld_button(u"btn_close"_ustr))
        , m_xBtnNewNb(m_xBuilder->weld_button(u"btn_new_nb"_ustr))
        , m_xBtnRenameNb(m_xBuilder->weld_button(u"btn_rename_nb"_ustr))
        , m_xChipSum(m_xBuilder->weld_button(u"chip_summary"_ustr))
        , m_xChipKey(m_xBuilder->weld_button(u"chip_key"_ustr))
        , m_xChipCmp(m_xBuilder->weld_button(u"chip_compare"_ustr))
        , m_xChipFaq(m_xBuilder->weld_button(u"chip_faq"_ustr))
        , m_xBtnAudio(m_xBuilder->weld_button(u"btn_audio"_ustr))
        , m_xBtnVideo(m_xBuilder->weld_button(u"btn_video"_ustr))
        , m_xBtnGuide(m_xBuilder->weld_button(u"btn_guide"_ustr))
        , m_xBtnBrief(m_xBuilder->weld_button(u"btn_brief"_ustr))
        , m_xBtnStudioFaq(m_xBuilder->weld_button(u"btn_studio_faq"_ustr))
        , m_xBtnTimeline(m_xBuilder->weld_button(u"btn_timeline"_ustr))
        , m_xBtnFlashcards(m_xBuilder->weld_button(u"btn_flashcards"_ustr))
        , m_xBtnMindmap(m_xBuilder->weld_button(u"btn_mindmap"_ustr))
        , m_xBtnSaveNote(m_xBuilder->weld_button(u"btn_save_note"_ustr))
        , m_xBtnSpeak(m_xBuilder->weld_button(u"btn_speak"_ustr))
        , m_xBtnExportAudio(m_xBuilder->weld_button(u"btn_export_audio"_ustr))
        , m_xBtnToWriter(m_xBuilder->weld_button(u"btn_to_writer"_ustr))
        , m_xBtnToImpress(m_xBuilder->weld_button(u"btn_to_impress"_ustr))
        , m_xHeaderBox(m_xBuilder->weld_container(u"header_box"_ustr))
        , m_xBrandLabel(m_xBuilder->weld_label(u"brand_label"_ustr))
        , m_xBrandSub(m_xBuilder->weld_label(u"brand_sub"_ustr))
        , m_xSourcesFrame(m_xBuilder->weld_container(u"sources_frame"_ustr))
        , m_xChatFrame(m_xBuilder->weld_container(u"chat_frame"_ustr))
        , m_xStudioFrame(m_xBuilder->weld_container(u"studio_frame"_ustr))
        , m_xMainCols(m_xBuilder->weld_container(u"main_cols"_ustr))
    {
        applyFamilyTheme();
        if (m_xSourceList)
        {
            m_xSourceList->enable_toggle_buttons(weld::ColumnToggleType::Check);
            m_xSourceList->set_selection_mode(SelectionMode::Single);
            m_xSourceList->connect_row_activated(LINK(this, KqNotebookController, OnSrcActivated));
        }
        auto bind = [](std::unique_ptr<weld::Button>& b, const auto& link) {
            if (b)
                b->connect_clicked(link);
        };
        bind(m_xBtnAddFile, LINK(this, KqNotebookController, OnAddFile));
        bind(m_xBtnAddPaste, LINK(this, KqNotebookController, OnAddPaste));
        bind(m_xBtnAddUrl, LINK(this, KqNotebookController, OnAddUrl));
        bind(m_xBtnAddTranscript, LINK(this, KqNotebookController, OnAddTranscript));
        bind(m_xBtnLocalAsr, LINK(this, KqNotebookController, OnLocalAsr));
        bind(m_xBtnSrcAll, LINK(this, KqNotebookController, OnSrcAll));
        bind(m_xBtnSrcPreview, LINK(this, KqNotebookController, OnSrcPreview));
        bind(m_xBtnSrcRemove, LINK(this, KqNotebookController, OnSrcRemove));
        bind(m_xBtnTxLoad, LINK(this, KqNotebookController, OnTxLoad));
        bind(m_xBtnTxSave, LINK(this, KqNotebookController, OnTxSave));
        bind(m_xBtnTxMedia, LINK(this, KqNotebookController, OnTxMedia));
        bind(m_xBtnTxMp3, LINK(this, KqNotebookController, OnTxMp3));
        bind(m_xBtnTxWav, LINK(this, KqNotebookController, OnTxWav));
        bind(m_xBtnTxChapters, LINK(this, KqNotebookController, OnTxChapters));
        bind(m_xBtnTxAiMinutes, LINK(this, KqNotebookController, OnTxAiMinutes));
        bind(m_xBtnTxAiPolish, LINK(this, KqNotebookController, OnTxAiPolish));
        bind(m_xBtnTxAiTodo, LINK(this, KqNotebookController, OnTxAiTodo));
        bind(m_xBtnTxSpeak, LINK(this, KqNotebookController, OnTxSpeak));
        bind(m_xBtnTxDiarize, LINK(this, KqNotebookController, OnTxDiarize));
        bind(m_xBtnTxRename, LINK(this, KqNotebookController, OnTxRename));
        bind(m_xBtnTxPlayCue, LINK(this, KqNotebookController, OnTxPlayCue));
        bind(m_xBtnTxMergeAdj, LINK(this, KqNotebookController, OnTxMergeAdj));
        bind(m_xBtnTxMergeNext, LINK(this, KqNotebookController, OnTxMergeNext));
        bind(m_xBtnTxCycleSpk, LINK(this, KqNotebookController, OnTxCycleSpk));
        bind(m_xBtnTxDelCue, LINK(this, KqNotebookController, OnTxDelCue));
        bind(m_xBtnTxUp, LINK(this, KqNotebookController, OnTxUp));
        bind(m_xBtnTxDown, LINK(this, KqNotebookController, OnTxDown));
        if (m_xSpkCount)
            m_xSpkCount->set_active(0);
        if (m_xTimelineList)
            m_xTimelineList->connect_selection_changed(
                LINK(this, KqNotebookController, OnTimelineChanged));
        bind(m_xBtnSend, LINK(this, KqNotebookController, OnSend));
        bind(m_xBtnVoice, LINK(this, KqNotebookController, OnVoice));
        bind(m_xBtnMeeting, LINK(this, KqNotebookController, OnMeeting));
        bind(m_xBtnTtsReply, LINK(this, KqNotebookController, OnTtsReply));
        bind(m_xBtnClose, LINK(this, KqNotebookController, OnClose));
        bind(m_xBtnNewNb, LINK(this, KqNotebookController, OnNewNb));
        bind(m_xBtnRenameNb, LINK(this, KqNotebookController, OnRenameNb));
        bind(m_xChipSum, LINK(this, KqNotebookController, OnChipSum));
        bind(m_xChipKey, LINK(this, KqNotebookController, OnChipKey));
        bind(m_xChipCmp, LINK(this, KqNotebookController, OnChipCmp));
        bind(m_xChipFaq, LINK(this, KqNotebookController, OnChipFaq));
        bind(m_xBtnAudio, LINK(this, KqNotebookController, OnAudio));
        bind(m_xBtnVideo, LINK(this, KqNotebookController, OnVideo));
        bind(m_xBtnGuide, LINK(this, KqNotebookController, OnGuide));
        bind(m_xBtnBrief, LINK(this, KqNotebookController, OnBrief));
        bind(m_xBtnStudioFaq, LINK(this, KqNotebookController, OnStudioFaq));
        bind(m_xBtnTimeline, LINK(this, KqNotebookController, OnTimeline));
        bind(m_xBtnFlashcards, LINK(this, KqNotebookController, OnFlashcards));
        bind(m_xBtnMindmap, LINK(this, KqNotebookController, OnMindmap));
        bind(m_xBtnSaveNote, LINK(this, KqNotebookController, OnSaveNote));
        bind(m_xBtnSpeak, LINK(this, KqNotebookController, OnSpeak));
        bind(m_xBtnExportAudio, LINK(this, KqNotebookController, OnExportAudio));
        bind(m_xBtnToWriter, LINK(this, KqNotebookController, OnToWriter));
        bind(m_xBtnToImpress, LINK(this, KqNotebookController, OnToImpress));
        if (m_xChatEntry)
            m_xChatEntry->connect_activate(LINK(this, KqNotebookController, OnEntryActivate));
        if (m_xNbCombo)
            m_xNbCombo->connect_changed(LINK(this, KqNotebookController, OnNbChanged));
        if (m_xArtifactList)
            m_xArtifactList->connect_selection_changed(
                LINK(this, KqNotebookController, OnArtChanged));

        m_aLiveTick.SetTimeout(2500);
        m_aLiveTick.SetInvokeHandler(LINK(this, KqNotebookController, OnLiveTick));

        ensureProject();
        reloadProjectCombo();
        loadActiveProject();
        UpdateModelStatus();
    }

    ~KqNotebookController() override
    {
        m_aLiveTick.Stop();
        if (LocalSpeechHub::isLiveMeeting())
        {
            OUString a, t, s;
            LocalSpeechHub::stopLiveMeeting(a, t, s);
        }
        persistProject();
    }

    /// Spokenly-style: put transcribed text into chat composer.
    void AcceptVoiceText(const OUString& rText)
    {
        if (rText.isEmpty())
            return;
        if (m_xChatEntry)
        {
            OUString cur = m_xChatEntry->get_text();
            if (!cur.isEmpty() && !cur.endsWith(u" "_ustr) && !cur.endsWith(u"\n"_ustr))
                cur += u" "_ustr;
            m_xChatEntry->set_text(cur + rText);
            m_xChatEntry->grab_focus();
        }
        if (m_xChatStatus)
            m_xChatStatus->set_label(u"🎤 已填入语音 · 确认后发送（Enter）"_ustr);
        if (m_xBtnVoice)
            m_xBtnVoice->set_label(u"🎤 语音"_ustr);
        AppendChat(u"系统"_ustr, u"语音已填入输入框（本机转写 · 未上传）。"_ustr);
    }

private:
    DECL_LINK(OnAddFile, weld::Button&, void);
    DECL_LINK(OnAddPaste, weld::Button&, void);
    DECL_LINK(OnAddUrl, weld::Button&, void);
    DECL_LINK(OnAddTranscript, weld::Button&, void);
    DECL_LINK(OnLocalAsr, weld::Button&, void);
    DECL_LINK(OnSrcAll, weld::Button&, void);
    DECL_LINK(OnSrcPreview, weld::Button&, void);
    DECL_LINK(OnSrcRemove, weld::Button&, void);
    DECL_LINK(OnTxLoad, weld::Button&, void);
    DECL_LINK(OnTxSave, weld::Button&, void);
    DECL_LINK(OnTxMedia, weld::Button&, void);
    DECL_LINK(OnTxMp3, weld::Button&, void);
    DECL_LINK(OnTxWav, weld::Button&, void);
    DECL_LINK(OnTxChapters, weld::Button&, void);
    DECL_LINK(OnTxAiMinutes, weld::Button&, void);
    DECL_LINK(OnTxAiPolish, weld::Button&, void);
    DECL_LINK(OnTxAiTodo, weld::Button&, void);
    DECL_LINK(OnTxSpeak, weld::Button&, void);
    DECL_LINK(OnTxDiarize, weld::Button&, void);
    DECL_LINK(OnTxRename, weld::Button&, void);
    DECL_LINK(OnTxPlayCue, weld::Button&, void);
    DECL_LINK(OnTxMergeAdj, weld::Button&, void);
    DECL_LINK(OnTxMergeNext, weld::Button&, void);
    DECL_LINK(OnTxCycleSpk, weld::Button&, void);
    DECL_LINK(OnTxDelCue, weld::Button&, void);
    DECL_LINK(OnTxUp, weld::Button&, void);
    DECL_LINK(OnTxDown, weld::Button&, void);
    DECL_LINK(OnTimelineChanged, weld::TreeView&, void);
    DECL_LINK(OnSend, weld::Button&, void);
    DECL_LINK(OnVoice, weld::Button&, void);
    DECL_LINK(OnMeeting, weld::Button&, void);
    DECL_LINK(OnTtsReply, weld::Button&, void);
    DECL_LINK(OnLiveTick, Timer*, void);
    DECL_LINK(OnClose, weld::Button&, void);
    DECL_LINK(OnNewNb, weld::Button&, void);
    DECL_LINK(OnRenameNb, weld::Button&, void);
    DECL_LINK(OnChipSum, weld::Button&, void);
    DECL_LINK(OnChipKey, weld::Button&, void);
    DECL_LINK(OnChipCmp, weld::Button&, void);
    DECL_LINK(OnChipFaq, weld::Button&, void);
    DECL_LINK(OnAudio, weld::Button&, void);
    DECL_LINK(OnVideo, weld::Button&, void);
    DECL_LINK(OnGuide, weld::Button&, void);
    DECL_LINK(OnBrief, weld::Button&, void);
    DECL_LINK(OnStudioFaq, weld::Button&, void);
    DECL_LINK(OnTimeline, weld::Button&, void);
    DECL_LINK(OnFlashcards, weld::Button&, void);
    DECL_LINK(OnMindmap, weld::Button&, void);
    DECL_LINK(OnSaveNote, weld::Button&, void);
    DECL_LINK(OnSpeak, weld::Button&, void);
    DECL_LINK(OnExportAudio, weld::Button&, void);
    DECL_LINK(OnToWriter, weld::Button&, void);
    DECL_LINK(OnToImpress, weld::Button&, void);
    DECL_LINK(OnEntryActivate, weld::Entry&, bool);
    DECL_LINK(OnNbChanged, weld::ComboBox&, void);
    DECL_LINK(OnArtChanged, weld::TreeView&, void);
    DECL_LINK(OnSrcActivated, weld::TreeView&, bool);

    void applyFamilyTheme();
    void ensureProject();
    void reloadProjectCombo();
    void loadActiveProject();
    void persistProject();
    void syncSourcesFromUi();
    void ReloadSources();
    void ReloadArtifacts();
    void ReloadChatView();
    void UpdateModelStatus();
    void AppendChat(const OUString& rWho, const OUString& rText);
    void AppendStudio(const OUString& rKind, const OUString& rTitle, const OUString& rText);
    void previewSourceById(const OUString& rId, sal_Int32 nIndex1Based);
    OUString buildSourceContext(sal_Int32 nMaxChars = 24000) const;
    std::vector<OUString> selectedSourceIds() const;
    void askGrounded(const OUString& rUserQuestion);
    void runStudio(const OUString& rKind, const OUString& rInstruction);
    /// Run local ASR on material id; merge snippet; returns true on success.
    bool applyLocalAsrToMaterial(const OUString& rMaterialId, bool bQuietIfToolsMissing);
    /// Best-effort write companion .srt next to media for future imports.
    static void tryWriteSidecarSrt(const OUString& rMediaPath, const OUString& rPlainText);
    OUString selectedSourceId() const;
    OUString transcriptEditorText() const;
    void setTranscriptEditor(const OUString& rText);
    void runAiOnTranscript(const OUString& rKind, const OUString& rInstruction);
    OUString mediaPathOfSelected() const;
    void convertSelectedMedia(const OUString& rFormat);
    sal_Int32 selectedSpeakerCount() const;
    void rebuildTimelineFromEditor();
    void jumpToCue(sal_Int32 nIndex);
    sal_Int32 selectedTimelineIndex() const;
    void applyTimelineCues(std::vector<MediaTranscriptService::TranscriptCue> aCues,
                           sal_Int32 nSelectIndex = -1);
    /// Current Studio body (selected artifact or m_sStudioArtifact).
    OUString currentArtifactBody() const;
    OUString currentArtifactTitle() const;
    /// Confirm dialog then apply plan (human-in-the-loop).
    bool confirmMaterialize(const OUString& rTargetZh, const OUString& rPreview);
    bool openFactoryDoc(const OUString& rFactoryUrl);
    bool materializeToWriter(const OUString& rBody, const OUString& rTitle);
    bool materializeToImpress(const OUString& rBody, const OUString& rTitle);

    std::unique_ptr<weld::ComboBox> m_xNbCombo;
    std::unique_ptr<weld::TreeView> m_xSourceList;
    std::unique_ptr<weld::Label> m_xSrcStatus;
    std::unique_ptr<weld::TextView> m_xTranscriptEdit;
    std::unique_ptr<weld::TextView> m_xChatView;
    std::unique_ptr<weld::Entry> m_xChatEntry;
    std::unique_ptr<weld::Label> m_xChatStatus;
    std::unique_ptr<weld::TextView> m_xStudioView;
    std::unique_ptr<weld::TreeView> m_xArtifactList;
    std::unique_ptr<weld::Button> m_xBtnAddFile;
    std::unique_ptr<weld::Button> m_xBtnAddPaste;
    std::unique_ptr<weld::Button> m_xBtnAddUrl;
    std::unique_ptr<weld::Button> m_xBtnAddTranscript;
    std::unique_ptr<weld::Button> m_xBtnLocalAsr;
    std::unique_ptr<weld::Button> m_xBtnSrcAll;
    std::unique_ptr<weld::Button> m_xBtnSrcPreview;
    std::unique_ptr<weld::Button> m_xBtnSrcRemove;
    std::unique_ptr<weld::Button> m_xBtnTxLoad;
    std::unique_ptr<weld::Button> m_xBtnTxSave;
    std::unique_ptr<weld::Button> m_xBtnTxMedia;
    std::unique_ptr<weld::Button> m_xBtnTxMp3;
    std::unique_ptr<weld::Button> m_xBtnTxWav;
    std::unique_ptr<weld::Button> m_xBtnTxChapters;
    std::unique_ptr<weld::Button> m_xBtnTxAiMinutes;
    std::unique_ptr<weld::Button> m_xBtnTxAiPolish;
    std::unique_ptr<weld::Button> m_xBtnTxAiTodo;
    std::unique_ptr<weld::Button> m_xBtnTxSpeak;
    std::unique_ptr<weld::Button> m_xBtnTxDiarize;
    std::unique_ptr<weld::ComboBox> m_xSpkCount;
    std::unique_ptr<weld::Entry> m_xSpeakerNames;
    std::unique_ptr<weld::Button> m_xBtnTxRename;
    std::unique_ptr<weld::Button> m_xBtnTxPlayCue;
    std::unique_ptr<weld::Button> m_xBtnTxMergeAdj;
    std::unique_ptr<weld::Button> m_xBtnTxMergeNext;
    std::unique_ptr<weld::Button> m_xBtnTxCycleSpk;
    std::unique_ptr<weld::Button> m_xBtnTxDelCue;
    std::unique_ptr<weld::Button> m_xBtnTxUp;
    std::unique_ptr<weld::Button> m_xBtnTxDown;
    std::unique_ptr<weld::TreeView> m_xTimelineList;
    std::unique_ptr<weld::Button> m_xBtnSend;
    Timer m_aLiveTick;
    std::vector<MediaTranscriptService::TranscriptCue> m_aTimeline;
    std::unique_ptr<weld::Button> m_xBtnVoice;
    std::unique_ptr<weld::Button> m_xBtnMeeting;
    std::unique_ptr<weld::Button> m_xBtnTtsReply;
    std::unique_ptr<weld::Button> m_xBtnClose;
    std::unique_ptr<weld::Button> m_xBtnNewNb;
    std::unique_ptr<weld::Button> m_xBtnRenameNb;
    std::unique_ptr<weld::Button> m_xChipSum;
    std::unique_ptr<weld::Button> m_xChipKey;
    std::unique_ptr<weld::Button> m_xChipCmp;
    std::unique_ptr<weld::Button> m_xChipFaq;
    std::unique_ptr<weld::Button> m_xBtnAudio;
    std::unique_ptr<weld::Button> m_xBtnVideo;
    std::unique_ptr<weld::Button> m_xBtnGuide;
    std::unique_ptr<weld::Button> m_xBtnBrief;
    std::unique_ptr<weld::Button> m_xBtnStudioFaq;
    std::unique_ptr<weld::Button> m_xBtnTimeline;
    std::unique_ptr<weld::Button> m_xBtnFlashcards;
    std::unique_ptr<weld::Button> m_xBtnMindmap;
    std::unique_ptr<weld::Button> m_xBtnSaveNote;
    std::unique_ptr<weld::Button> m_xBtnSpeak;
    std::unique_ptr<weld::Button> m_xBtnExportAudio;
    std::unique_ptr<weld::Button> m_xBtnToWriter;
    std::unique_ptr<weld::Button> m_xBtnToImpress;
    std::unique_ptr<weld::Container> m_xHeaderBox;
    std::unique_ptr<weld::Label> m_xBrandLabel;
    std::unique_ptr<weld::Label> m_xBrandSub;
    std::unique_ptr<weld::Container> m_xSourcesFrame;
    std::unique_ptr<weld::Container> m_xChatFrame;
    std::unique_ptr<weld::Container> m_xStudioFrame;
    std::unique_ptr<weld::Container> m_xMainCols;
    NotebookProject m_aProject;
    OUString m_sStudioArtifact;
    OUString m_sStudioKind;
    OUString m_sTranscriptMaterialId; ///< editor bound material
    bool m_bBusy = false;
    bool m_bLoading = false;
};

void KqNotebookController::applyFamilyTheme()
{
    // Align with Start Center / Clavue family tokens (premium business calm).
    const auto th = sfx2::sc_theme::tokens();
    if (m_xDialog)
        m_xDialog->set_background(th.canvas);
    if (m_xHeaderBox)
        m_xHeaderBox->set_background(th.rail);
    if (m_xMainCols)
        m_xMainCols->set_background(th.canvas);
    if (m_xSourcesFrame)
        m_xSourcesFrame->set_background(th.rail);
    if (m_xChatFrame)
        m_xChatFrame->set_background(th.card);
    if (m_xStudioFrame)
        m_xStudioFrame->set_background(th.rail);
    if (m_xBrandLabel)
        m_xBrandLabel->set_font_color(th.accent);
    if (m_xBrandSub)
        m_xBrandSub->set_font_color(th.textSecondary);
    if (m_xSrcStatus)
        m_xSrcStatus->set_font_color(th.textTertiary);
    if (m_xChatStatus)
        m_xChatStatus->set_font_color(th.textSecondary);
    // Content surfaces: soft paper, not harsh system insets
    if (m_xChatView)
        m_xChatView->set_background(th.card);
    if (m_xStudioView)
        m_xStudioView->set_background(th.card);
    if (m_xTranscriptEdit)
        m_xTranscriptEdit->set_background(th.card);
    if (m_xSourceList)
        m_xSourceList->set_background(th.card);
    if (m_xArtifactList)
        m_xArtifactList->set_background(th.card);
    if (m_xTimelineList)
        m_xTimelineList->set_background(th.card);
}

void KqNotebookController::ensureProject()
{
    auto list = NotebookProjectStore::listProjects();
    if (list.empty())
    {
        m_aProject = NotebookProjectStore::createProject(u"我的第一个笔记本"_ustr);
        // migrate global materials into first project
        for (const auto& e : NotebookMaterialStore::listMaterials())
            m_aProject.materialIds.push_back(e.id);
        NotebookProjectStore::saveProject(m_aProject);
    }
    else
    {
        OUString active = NotebookProjectStore::activeProjectId();
        if (active.isEmpty())
            active = list.front().id;
        m_aProject = NotebookProjectStore::loadProject(active);
        if (m_aProject.id.isEmpty())
            m_aProject = NotebookProjectStore::loadProject(list.front().id);
        NotebookProjectStore::setActiveProjectId(m_aProject.id);
    }
}

void KqNotebookController::reloadProjectCombo()
{
    if (!m_xNbCombo)
        return;
    m_bLoading = true;
    m_xNbCombo->clear();
    const auto list = NotebookProjectStore::listProjects();
    int active = 0;
    for (size_t i = 0; i < list.size(); ++i)
    {
        m_xNbCombo->append(list[i].id, list[i].title);
        if (list[i].id == m_aProject.id)
            active = static_cast<int>(i);
    }
    if (!list.empty())
        m_xNbCombo->set_active(active);
    m_bLoading = false;
}

void KqNotebookController::loadActiveProject()
{
    m_bLoading = true;
    ReloadSources();
    ReloadChatView();
    ReloadArtifacts();
    if (m_xChatView && m_aProject.chat.empty())
    {
        AppendChat(u"可圈笔记"_ustr,
                   u"欢迎使用可圈笔记。\n"
                   u"① 来源：文件·URL·本地视频·字幕·🎤语音·🎙会议录音\n"
                   u"② 对话：提问 / 语音输入 · 🔊 朗读回答\n"
                   u"③ Studio：音频/视频概览、纪要、闪卡、导图…\n"
                   u"语音转写与朗读均本机处理，不静默上传。"_ustr);
    }
    m_bLoading = false;
    UpdateModelStatus();
}

void KqNotebookController::persistProject()
{
    if (m_aProject.id.isEmpty())
        return;
    syncSourcesFromUi();
    NotebookProjectStore::saveProject(m_aProject);
    NotebookProjectStore::setActiveProjectId(m_aProject.id);
}

void KqNotebookController::syncSourcesFromUi()
{
    m_aProject.materialIds = selectedSourceIds();
}

void KqNotebookController::UpdateModelStatus()
{
    if (!m_xChatStatus)
        return;
    if (kqoffice::ai::chat::DocumentAIVoiceInput::isListening())
    {
        m_xChatStatus->set_label(kqoffice::ai::chat::DocumentAIVoiceInput::statusHint());
        return;
    }
    if (LocalSpeechHub::isLiveMeeting())
    {
        m_xChatStatus->set_label(u"边录边出字中… 说话即可在转写区见字 · 再点「会议」结束"_ustr);
        return;
    }
    if (LocalSpeechHub::isMeetingRecording())
    {
        m_xChatStatus->set_label(LocalSpeechHub::meetingStatusHint());
        return;
    }
    const kqoffice::ai::ModelRoutingDiagnostics d = kqoffice::ai::diagnoseModelRouting();
    const SpeechCapability sp = LocalSpeechHub::diagnose();
    OUString title = m_aProject.title;
    OUString base = d.healthy ? u"可圈 AI · 就绪"_ustr : u"可圈 AI · 需配置"_ustr;
    base += u" · 笔记本「"_ustr + title + u"」"_ustr;
    if (sp.hasStt && sp.hasMicRecord)
        base += u" · 语音就绪"_ustr;
    m_xChatStatus->set_label(base);
}

void KqNotebookController::ReloadSources()
{
    if (!m_xSourceList)
        return;
    m_xSourceList->clear();
    // Only materials bound to this notebook (empty notebook stays empty).
    const std::vector<OUString>& ids = m_aProject.materialIds;
    sal_Int32 n = 0;
    for (const auto& id : ids)
    {
        const NotebookMaterial m = NotebookMaterialStore::loadMaterial(id);
        if (m.id.isEmpty())
            continue;
        const OUString label = u"["_ustr + OUString::number(n + 1) + u"] "_ustr + m.title + u" · "
                               + m.kind;
        m_xSourceList->append(m.id, label);
        const int row = m_xSourceList->n_children() - 1;
        if (row >= 0)
            m_xSourceList->set_toggle(row, TRISTATE_TRUE);
        ++n;
    }
    if (m_xSrcStatus)
        m_xSrcStatus->set_label(OUString::number(n) + u" 个来源（勾选后参与问答）"_ustr);
}

void KqNotebookController::ReloadChatView()
{
    if (!m_xChatView)
        return;
    OUStringBuffer b;
    for (const auto& t : m_aProject.chat)
    {
        OUString who = t.role;
        if (who == u"user"_ustr)
            who = u"你"_ustr;
        else if (who == u"assistant"_ustr)
            who = u"可圈 AI"_ustr;
        else if (who == u"system"_ustr)
            who = u"系统"_ustr;
        b.append(u"\n【"_ustr + who + u"】\n"_ustr + t.text + u"\n"_ustr);
    }
    m_xChatView->set_text(b.makeStringAndClear());
}

void KqNotebookController::ReloadArtifacts()
{
    if (!m_xArtifactList)
        return;
    m_xArtifactList->clear();
    for (const auto& a : m_aProject.artifacts)
        m_xArtifactList->append(a.id, a.kind + u" · "_ustr + a.title);
    if (!m_aProject.artifacts.empty())
    {
        const auto& last = m_aProject.artifacts.back();
        m_sStudioArtifact = last.body;
        m_sStudioKind = last.kind;
        if (m_xStudioView)
            m_xStudioView->set_text(last.body);
    }
}

std::vector<OUString> KqNotebookController::selectedSourceIds() const
{
    std::vector<OUString> ids;
    if (!m_xSourceList)
        return ids;
    const int n = m_xSourceList->n_children();
    for (int i = 0; i < n; ++i)
    {
        if (m_xSourceList->get_toggle(i) == TRISTATE_TRUE)
            ids.push_back(m_xSourceList->get_id(i));
    }
    return ids;
}

void KqNotebookController::previewSourceById(const OUString& rId, sal_Int32 nIndex1Based)
{
    const NotebookMaterial m = NotebookMaterialStore::loadMaterial(rId);
    if (m.id.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"无法加载该来源。"_ustr);
        return;
    }
    OUString body = m.snippet;
    if (body.isEmpty())
        body = u"（无可提取文本）"_ustr;
    // full text into editable transcript workspace (competitor: click → edit)
    setTranscriptEditor(MediaTranscriptService::normalizeTranscriptEdit(body));
    m_sTranscriptMaterialId = rId;
    OUString preview = body;
    if (preview.getLength() > 6000)
        preview = preview.copy(0, 6000) + u"…"_ustr;
    const OUString view = u"# 来源 ["_ustr + OUString::number(nIndex1Based) + u"] 预览\n\n**"_ustr
                          + m.title + u"** · "_ustr + m.kind + u"\n\n"_ustr + preview;
    if (m_xStudioView)
        m_xStudioView->set_text(view);
    if (m_xChatStatus)
        m_xChatStatus->set_label(u"引用定位 · ["_ustr + OUString::number(nIndex1Based) + u"] "
                                 + m.title + u" · 可在下方编辑转写"_ustr);
    AppendChat(u"系统"_ustr,
               u"已打开来源 ["_ustr + OUString::number(nIndex1Based) + u"]："_ustr + m.title
                   + u"。转写编辑区可改，点「保存」写回。"_ustr);
}

OUString KqNotebookController::buildSourceContext(sal_Int32 nMaxChars) const
{
    OUStringBuffer b;
    sal_Int32 used = 0;
    sal_Int32 idx = 1;
    for (const auto& id : selectedSourceIds())
    {
        const NotebookMaterial m = NotebookMaterialStore::loadMaterial(id);
        if (m.id.isEmpty())
            continue;
        OUString chunk = u"\n\n["_ustr + OUString::number(idx) + u"] "_ustr + m.title + u"\n"_ustr;
        OUString body = m.snippet;
        if (body.isEmpty())
            body = u"（无可提取文本）"_ustr;
        const sal_Int32 room = nMaxChars - used - chunk.getLength();
        if (room <= 0)
            break;
        if (body.getLength() > room)
            body = body.copy(0, room) + u"…"_ustr;
        chunk += body;
        b.append(chunk);
        used += chunk.getLength();
        ++idx;
    }
    return b.makeStringAndClear();
}

void KqNotebookController::AppendChat(const OUString& rWho, const OUString& rText)
{
    ChatTurn t;
    if (rWho == u"你"_ustr)
        t.role = u"user"_ustr;
    else if (rWho == u"可圈 AI"_ustr)
        t.role = u"assistant"_ustr;
    else
        t.role = u"system"_ustr;
    t.text = rText;
    m_aProject.chat.push_back(t);
    // keep last 40 turns
    if (m_aProject.chat.size() > 40)
        m_aProject.chat.erase(m_aProject.chat.begin(),
                              m_aProject.chat.begin()
                                  + static_cast<std::ptrdiff_t>(m_aProject.chat.size() - 40));
    if (!m_xChatView)
        return;
    OUString cur = m_xChatView->get_text();
    if (!cur.isEmpty() && !cur.endsWith(u"\n"_ustr))
        cur += u"\n"_ustr;
    cur += u"\n【"_ustr + rWho + u"】\n"_ustr + rText + u"\n"_ustr;
    m_xChatView->set_text(cur);
}

void KqNotebookController::AppendStudio(const OUString& rKind, const OUString& rTitle,
                                        const OUString& rText)
{
    m_sStudioArtifact = rText;
    m_sStudioKind = rKind;
    if (m_xStudioView)
        m_xStudioView->set_text(rText);
    StudioArtifact a;
    a.id = NotebookProjectStore::newId();
    a.kind = rKind;
    a.title = rTitle;
    a.body = rText;
    // stamp filled on save; keep a non-empty local marker for UI list
    a.createdIso = m_aProject.updatedIso.isEmpty() ? a.id : m_aProject.updatedIso;
    m_aProject.artifacts.push_back(a);
    if (m_aProject.artifacts.size() > 30)
        m_aProject.artifacts.erase(m_aProject.artifacts.begin());
    ReloadArtifacts();
    persistProject();
}

void KqNotebookController::askGrounded(const OUString& rUserQuestion)
{
    if (m_bBusy || rUserQuestion.isEmpty())
        return;
    const OUString sources = buildSourceContext();
    if (sources.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"请先在左侧添加并勾选至少一个来源。"_ustr);
        return;
    }
    AppendChat(u"你"_ustr, rUserQuestion);
    if (m_xChatStatus)
        m_xChatStatus->set_label(u"可圈 AI · 生成中…"_ustr);
    m_bBusy = true;
    Application::Reschedule(true);

    OUStringBuffer prompt;
    prompt.append(u"你是可圈笔记研究助手。仅依据用户勾选的来源回答，并标注引用。\n"
                  u"规则：1) 只根据来源回答 2) 结论后用 [编号] 引用 3) 不足则说明未找到 4) 中文简洁。\n"
                  u"===== 来源 =====\n"_ustr);
    prompt.append(sources);
    prompt.append(u"\n\n===== 用户问题 =====\n"_ustr);
    prompt.append(rUserQuestion);

    const kqoffice::ai::StreamChatResult r
        = kqoffice::ai::streamChatCompletion(u"chat"_ustr, prompt.makeStringAndClear(), {}, {});
    m_bBusy = false;
    if (r.status == u"ok"_ustr && !r.content.isEmpty())
        AppendChat(u"可圈 AI"_ustr, r.content);
    else
        AppendChat(u"系统"_ustr,
                   u"可圈 AI 暂时无法回答（"_ustr + r.status
                       + u"）。请到「工具 → 选项 → 可圈 AI」检查配置。"_ustr);
    persistProject();
    UpdateModelStatus();
}

void KqNotebookController::runStudio(const OUString& rKind, const OUString& rInstruction)
{
    if (m_bBusy)
        return;
    const OUString sources = buildSourceContext(20000);
    if (sources.isEmpty())
    {
        if (m_xStudioView)
            m_xStudioView->set_text(u"请先添加并勾选来源。"_ustr);
        return;
    }
    if (m_xChatStatus)
        m_xChatStatus->set_label(u"可圈 AI · Studio 生成中… · "_ustr + rKind);
    m_bBusy = true;
    Application::Reschedule(true);

    OUStringBuffer prompt;
    prompt.append(u"你是可圈笔记 Studio。仅依据来源产出 Markdown。\n类型："_ustr + rKind
                  + u"\n要求："_ustr + rInstruction
                  + u"\n引用用 [编号]。\n===== 来源 =====\n"_ustr);
    prompt.append(sources);

    const kqoffice::ai::StreamChatResult r
        = kqoffice::ai::streamChatCompletion(u"chat"_ustr, prompt.makeStringAndClear(), {}, {});
    m_bBusy = false;
    if (r.status == u"ok"_ustr && !r.content.isEmpty())
    {
        OUString body = r.content;
        // Audio scripts: normalize A/B for dual-voice product pipeline.
        if (rKind == u"audio-script"_ustr || rKind.indexOf(u"audio"_ustr) >= 0)
        {
            body = kqoffice::ai::notebook::NotebookStudioPipeline::normalizeDualVoiceScript(
                body);
            AppendChat(u"系统"_ustr,
                       kqoffice::ai::notebook::NotebookStudioPipeline::scoreScriptQualityZh(body));
        }
        AppendStudio(rKind, rKind + u" · "_ustr + m_aProject.title,
                     u"# "_ustr + rKind + u"\n\n"_ustr + body);
    }
    else if (m_xStudioView)
        m_xStudioView->set_text(u"Studio 生成失败（"_ustr + r.status + u"）。"_ustr);
    UpdateModelStatus();
}

IMPL_LINK_NOARG(KqNotebookController, OnClose, weld::Button&, void)
{
    persistProject();
    if (weld::Window* p = getDialog())
        p->hide();
}

IMPL_LINK_NOARG(KqNotebookController, OnNewNb, weld::Button&, void)
{
    persistProject();
    m_aProject = NotebookProjectStore::createProject(u"新笔记本"_ustr);
    reloadProjectCombo();
    if (m_xChatView)
        m_xChatView->set_text(OUString());
    if (m_xStudioView)
        m_xStudioView->set_text(OUString());
    m_aProject.chat.clear();
    m_aProject.artifacts.clear();
    m_aProject.materialIds.clear();
    loadActiveProject();
    AppendChat(u"系统"_ustr, u"已新建笔记本「"_ustr + m_aProject.title + u"」。请添加来源。"_ustr);
    persistProject();
}

IMPL_LINK_NOARG(KqNotebookController, OnRenameNb, weld::Button&, void)
{
    // Use chat entry as rename field if starts with rename: or simple increment title
    if (!m_xChatEntry)
        return;
    OUString t = m_xChatEntry->get_text().trim();
    if (t.isEmpty())
        t = m_aProject.title + u" (修订)"_ustr;
    m_aProject.title = t;
    m_xChatEntry->set_text(OUString());
    persistProject();
    reloadProjectCombo();
    AppendChat(u"系统"_ustr, u"笔记本已重命名为「"_ustr + m_aProject.title + u"」。"_ustr);
    UpdateModelStatus();
}

IMPL_LINK_NOARG(KqNotebookController, OnNbChanged, weld::ComboBox&, void)
{
    if (m_bLoading || !m_xNbCombo)
        return;
    const OUString id = m_xNbCombo->get_active_id();
    if (id.isEmpty() || id == m_aProject.id)
        return;
    persistProject();
    m_aProject = NotebookProjectStore::loadProject(id);
    NotebookProjectStore::setActiveProjectId(id);
    loadActiveProject();
}

IMPL_LINK_NOARG(KqNotebookController, OnArtChanged, weld::TreeView&, void)
{
    if (!m_xArtifactList)
        return;
    const OUString id = m_xArtifactList->get_selected_id();
    for (const auto& a : m_aProject.artifacts)
    {
        if (a.id == id)
        {
            m_sStudioArtifact = a.body;
            m_sStudioKind = a.kind;
            if (m_xStudioView)
                m_xStudioView->set_text(a.body);
            break;
        }
    }
}

IMPL_LINK_NOARG(KqNotebookController, OnAddFile, weld::Button&, void)
{
    sfx2::FileDialogHelper aDlg(css::ui::dialogs::TemplateDescription::FILEOPEN_SIMPLE,
                                FileDialogFlags::NONE, m_xDialog.get());
    aDlg.SetTitle(u"可圈笔记 · 添加来源文件"_ustr);
    aDlg.AddFilter(u"文本与数据"_ustr, u"*.txt;*.md;*.markdown;*.csv;*.json"_ustr);
    aDlg.AddFilter(u"PDF"_ustr, u"*.pdf"_ustr);
    aDlg.AddFilter(u"办公文稿"_ustr, u"*.odt;*.ods;*.odp;*.docx;*.xlsx;*.pptx"_ustr);
    aDlg.AddFilter(u"本地视频"_ustr,
                   u"*.mp4;*.mov;*.mkv;*.webm;*.avi;*.m4v;*.mpeg;*.mpg;*.flv;*.ts;*.3gp"_ustr);
    aDlg.AddFilter(u"本地音频"_ustr,
                   u"*.mp3;*.wav;*.m4a;*.aac;*.flac;*.ogg;*.opus;*.aiff;*.wma;*.amr;*.caf"_ustr);
    aDlg.AddFilter(u"字幕"_ustr, u"*.srt;*.vtt;*.ass"_ustr);
    aDlg.AddFilter(u"所有文件"_ustr, u"*.*"_ustr);
    if (aDlg.Execute() != ERRCODE_NONE)
        return;
    OUString path = aDlg.GetPath();
    if (path.isEmpty())
        return;
    if (path.startsWith(u"file:"_ustr))
    {
        OUString sys;
        if (osl::FileBase::getSystemPathFromFileURL(path, sys) == osl::FileBase::E_None)
            path = sys;
    }
    const NotebookMaterial m = NotebookMaterialStore::importFile(path);
    if (m.id.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"导入失败，请换格式重试。"_ustr);
        return;
    }
    m_aProject.materialIds.push_back(m.id);
    ReloadSources();
    // select new source in list
    if (m_xSourceList)
    {
        const int n = m_xSourceList->n_children();
        for (int i = 0; i < n; ++i)
        {
            if (m_xSourceList->get_id(i) == m.id)
            {
                m_xSourceList->set_cursor(i);
                break;
            }
        }
    }
    OUString tip = u"已添加来源："_ustr + m.title + u" · "_ustr + m.kind;
    if (m.kind == u"video"_ustr || m.kind == u"audio"_ustr)
    {
        if (m.snippet.indexOf(u"同名字幕"_ustr) >= 0)
            tip += u"。已自动挂接同名字幕/转录。"_ustr;
        else
        {
            const auto d = NotebookMaterialStore::diagnoseLocalAsr();
            tip += u"。未找到同名字幕。\n"_ustr + d.summary;
            AppendChat(u"系统"_ustr, tip);
            persistProject();
            reloadProjectCombo();
            if (d.hasFfmpeg && d.hasWhisper)
            {
                std::unique_ptr<weld::MessageDialog> xAsk(Application::CreateMessageDialog(
                    m_xDialog.get(), VclMessageType::Question, VclButtonsType::YesNo,
                    u"未检测到同名字幕。是否立即用本机 ffmpeg + whisper 转写？\n"
                    u"（仅在本机运行，不会上传。可稍后点「本机转写」。）"_ustr));
                if (xAsk && xAsk->run() == RET_YES)
                    applyLocalAsrToMaterial(m.id, false);
            }
            else
            {
                AppendChat(u"系统"_ustr,
                           u"可「字幕/转录」粘贴，或安装工具后点「本机转写」。\n"_ustr
                               + d.installHint);
            }
            return;
        }
    }
    AppendChat(u"系统"_ustr, tip);
    persistProject();
    reloadProjectCombo();
}

IMPL_LINK_NOARG(KqNotebookController, OnAddPaste, weld::Button&, void)
{
    OUString body;
    if (m_xChatEntry)
        body = m_xChatEntry->get_text().trim();
    if (body.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"请先在输入框粘贴文本，再点「粘贴文本」。"_ustr);
        return;
    }
    const NotebookMaterial m = NotebookMaterialStore::importText(u"粘贴来源"_ustr, body);
    if (m_xChatEntry)
        m_xChatEntry->set_text(OUString());
    m_aProject.materialIds.push_back(m.id);
    ReloadSources();
    AppendChat(u"系统"_ustr, u"已添加粘贴来源。"_ustr);
    persistProject();
}

IMPL_LINK_NOARG(KqNotebookController, OnAddUrl, weld::Button&, void)
{
    OUString url;
    if (m_xChatEntry)
        url = m_xChatEntry->get_text().trim();
    if (url.isEmpty() || !(url.startsWith(u"http://"_ustr) || url.startsWith(u"https://"_ustr)))
    {
        AppendChat(u"系统"_ustr, u"请在输入框粘贴 http(s) 网址，再点「+ 网页 URL」。"_ustr);
        return;
    }
    // YouTube / short-video sites: no silent cloud fetch — guide local remedy
    const OUString low = url.toAsciiLowerCase();
    if (low.indexOf(u"youtube.com"_ustr) >= 0 || low.indexOf(u"youtu.be"_ustr) >= 0
        || low.indexOf(u"bilibili.com"_ustr) >= 0 || low.indexOf(u"vimeo.com"_ustr) >= 0)
    {
        AppendChat(
            u"系统"_ustr,
            u"检测到视频网站链接。可圈笔记不直接拉取 YouTube 等云端视频。\n"
            u"补救（本地优先）：\n"
            u"① 下载视频到本机 →「+ 添加文件」导入\n"
            u"② 导出/复制字幕（SRT/VTT 或纯文本）→「字幕/转录」粘贴或打开文件\n"
            u"③ 把字幕与视频同名放在同一文件夹，导入视频时会自动挂接\n"
            u"链接已记为参考："_ustr
                + url);
        // still store as lightweight URL note so project remembers the link
        const NotebookMaterial m = NotebookMaterialStore::importText(
            u"视频链接（需本地字幕）· "_ustr + url,
            u"来源链接: "_ustr + url
                + u"\n\n（未抓取页面。请用「字幕/转录」或本地视频+同名.srt 补齐可引用正文。）"_ustr);
        m_aProject.materialIds.push_back(m.id);
        if (m_xChatEntry)
            m_xChatEntry->set_text(OUString());
        ReloadSources();
        persistProject();
        UpdateModelStatus();
        return;
    }
    if (m_xChatStatus)
        m_xChatStatus->set_label(u"正在抓取网页…"_ustr);
    Application::Reschedule(true);
    const OUString text = fetchUrlText(url);
    if (text.isEmpty())
    {
        AppendChat(u"系统"_ustr,
                   u"无法抓取该 URL（网络或页面限制）。可改为下载后「添加文件」，"
                   u"或把正文粘贴后点「粘贴文本」/「字幕/转录」。"_ustr);
        UpdateModelStatus();
        return;
    }
    const NotebookMaterial m
        = NotebookMaterialStore::importText(u"网页 · "_ustr + url, text);
    m_aProject.materialIds.push_back(m.id);
    if (m_xChatEntry)
        m_xChatEntry->set_text(OUString());
    ReloadSources();
    AppendChat(u"系统"_ustr, u"已添加网页来源（已提取文本）："_ustr + url);
    persistProject();
    UpdateModelStatus();
}

IMPL_LINK_NOARG(KqNotebookController, OnAddTranscript, weld::Button&, void)
{
    // Prefer paste from entry; if empty or short, open .srt/.vtt file dialog
    OUString body;
    if (m_xChatEntry)
        body = m_xChatEntry->get_text().trim();

    // If user pasted a youtube URL only, redirect guidance
    if (!body.isEmpty()
        && (body.startsWith(u"http://"_ustr) || body.startsWith(u"https://"_ustr))
        && body.getLength() < 200)
    {
        AppendChat(u"系统"_ustr,
                   u"「字幕/转录」需要字幕正文或 .srt/.vtt 文件，不是视频链接。\n"
                   u"请在 YouTube 打开字幕 → 复制文稿粘贴到输入框，或用下载器导出 SRT 后点此按钮选文件。"_ustr);
        return;
    }

    if (body.getLength() >= 20)
    {
        const NotebookMaterial m = NotebookMaterialStore::importTranscript(
            u"字幕/转录（粘贴）"_ustr, body, u"subtitle"_ustr);
        if (m.id.isEmpty())
        {
            AppendChat(u"系统"_ustr, u"字幕/转录导入失败。"_ustr);
            return;
        }
        if (m_xChatEntry)
            m_xChatEntry->set_text(OUString());
        m_aProject.materialIds.push_back(m.id);
        ReloadSources();
        AppendChat(u"系统"_ustr,
                   u"已添加字幕/转录来源（"_ustr + OUString::number(m.charCount)
                       + u" 字）。可与本地视频一并勾选后生成「视频概览」。"_ustr);
        persistProject();
        return;
    }

    sfx2::FileDialogHelper aDlg(css::ui::dialogs::TemplateDescription::FILEOPEN_SIMPLE,
                                FileDialogFlags::NONE, m_xDialog.get());
    aDlg.SetTitle(u"可圈笔记 · 打开字幕/转录文件"_ustr);
    aDlg.AddFilter(u"字幕"_ustr, u"*.srt;*.vtt;*.ass;*.txt"_ustr);
    aDlg.AddFilter(u"所有文件"_ustr, u"*.*"_ustr);
    if (aDlg.Execute() != ERRCODE_NONE)
    {
        AppendChat(u"系统"_ustr,
                   u"请先在输入框粘贴字幕/转录（≥20 字），或在对话框中选择 .srt/.vtt 文件。\n"
                   u"YouTube 补救：复制自动字幕 / 用本地下载工具导出 SRT。"_ustr);
        return;
    }
    OUString path = aDlg.GetPath();
    if (path.startsWith(u"file:"_ustr))
    {
        OUString sys;
        if (osl::FileBase::getSystemPathFromFileURL(path, sys) == osl::FileBase::E_None)
            path = sys;
    }
    const NotebookMaterial m = NotebookMaterialStore::importFile(path);
    if (m.id.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"字幕文件导入失败。"_ustr);
        return;
    }
    m_aProject.materialIds.push_back(m.id);
    ReloadSources();
    AppendChat(u"系统"_ustr, u"已添加字幕文件："_ustr + m.title);
    persistProject();
}

void KqNotebookController::tryWriteSidecarSrt(const OUString& rMediaPath,
                                              const OUString& rPlainText)
{
    if (rMediaPath.isEmpty() || rPlainText.isEmpty())
        return;
    sal_Int32 slash = rMediaPath.lastIndexOf(u'/');
#if defined(_WIN32)
    const sal_Int32 bslash = rMediaPath.lastIndexOf(u'\\');
    if (bslash > slash)
        slash = bslash;
#endif
    const OUString name = (slash >= 0) ? rMediaPath.copy(slash + 1) : rMediaPath;
    const sal_Int32 dot = name.lastIndexOf(u'.');
    const OUString base = (dot > 0) ? name.copy(0, dot) : name;
    const OUString dir = (slash > 0) ? rMediaPath.copy(0, slash) : OUString();
    if (dir.isEmpty() || base.isEmpty())
        return;
    const OUString sep = (rMediaPath.indexOf(u'\\') >= 0) ? u"\\"_ustr : u"/"_ustr;
    const OUString srtPath = dir + sep + base + u".srt"_ustr;
    // don't overwrite existing user subtitles
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(srtPath, url) == osl::FileBase::E_None)
    {
        osl::DirectoryItem item;
        if (osl::DirectoryItem::get(url, item) == osl::FileBase::E_None)
            return;
    }
    // minimal SRT (one block) for re-import
    OUString body = u"1\n00:00:00,000 --> 00:59:59,000\n"_ustr;
    // keep first ~8k chars to avoid huge sidecars
    OUString plain = rPlainText;
    if (plain.getLength() > 8000)
        plain = plain.copy(0, 8000) + u"…"_ustr;
    body += plain + u"\n"_ustr;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        return;
    f.setSize(0);
    const OString utf8 = OUStringToOString(body, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(utf8.getStr(), utf8.getLength(), n);
    f.close();
}

bool KqNotebookController::applyLocalAsrToMaterial(const OUString& rMaterialId,
                                                   bool bQuietIfToolsMissing)
{
    if (m_bBusy || rMaterialId.isEmpty())
        return false;
    const auto diag = NotebookMaterialStore::diagnoseLocalAsr();
    const NotebookMaterial mat = NotebookMaterialStore::loadMaterial(rMaterialId);
    if (!NotebookMaterialStore::isLocalMediaMaterial(mat))
    {
        if (!bQuietIfToolsMissing)
            AppendChat(u"系统"_ustr,
                       u"「本机转写」仅适用于本地视频/音频。当前: "_ustr + mat.kind);
        return false;
    }
    if (!diag.hasFfmpeg || !diag.hasWhisper)
    {
        if (!bQuietIfToolsMissing)
            AppendChat(u"系统"_ustr, diag.summary + u"\n"_ustr + diag.installHint);
        return false;
    }
    if (m_xChatStatus)
        m_xChatStatus->set_label(u"音视频转写中（多格式归一化→本机 STT，不上传）…"_ustr);
    AppendChat(u"系统"_ustr,
               u"开始转写「"_ustr + mat.title
                   + u"」… 支持多格式音视频；首次可能下载 whisper 模型。"_ustr);
    m_bBusy = true;
    Application::Reschedule(true);

    OUString status;
    const OUString text = MediaTranscriptService::transcribeMediaFile(
        mat.sourcePath, status, 900, []() { Application::Reschedule(true); });
    m_bBusy = false;

    if (text.isEmpty())
    {
        AppendChat(u"系统"_ustr, status.isEmpty() ? u"本机转写失败。"_ustr : status);
        UpdateModelStatus();
        return false;
    }

    OUString header = mat.snippet;
    const sal_Int32 cut = header.indexOf(u"\n\n---"_ustr);
    if (cut > 0)
        header = header.copy(0, cut);
    const OUString merged = header + u"\n\n--- 本机转写（"_ustr + diag.whisperKind
                            + u" · 未上传）---\n"_ustr + text;
    if (!NotebookMaterialStore::replaceSnippet(rMaterialId, merged))
    {
        AppendChat(u"系统"_ustr, u"转写成功但写入材料失败。"_ustr);
        UpdateModelStatus();
        return false;
    }
    tryWriteSidecarSrt(mat.sourcePath, text);
    ReloadSources();
    if (m_xSourceList)
    {
        const int n = m_xSourceList->n_children();
        for (int i = 0; i < n; ++i)
        {
            if (m_xSourceList->get_id(i) == rMaterialId)
            {
                m_xSourceList->set_cursor(i);
                m_xSourceList->set_toggle(i, TRISTATE_TRUE);
                previewSourceById(rMaterialId, i + 1);
                break;
            }
        }
    }
    // competitor UX: open in editable transcript workspace
    setTranscriptEditor(MediaTranscriptService::normalizeTranscriptEdit(text));
    m_sTranscriptMaterialId = rMaterialId;
    AppendChat(u"系统"_ustr, status + u"\n已写入来源「"_ustr + mat.title
                                    + u"」，并打开「转写编辑」可改/分章/AI 处理。"
                                      u"若目录可写已尝试保存同名 .srt。"_ustr);
    persistProject();
    UpdateModelStatus();
    return true;
}

OUString KqNotebookController::selectedSourceId() const
{
    if (!m_xSourceList)
        return OUString();
    return m_xSourceList->get_selected_id();
}

OUString KqNotebookController::transcriptEditorText() const
{
    if (!m_xTranscriptEdit)
        return OUString();
    return m_xTranscriptEdit->get_text();
}

void KqNotebookController::setTranscriptEditor(const OUString& rText)
{
    if (m_xTranscriptEdit)
        m_xTranscriptEdit->set_text(rText);
}

OUString KqNotebookController::mediaPathOfSelected() const
{
    const OUString id = selectedSourceId();
    if (id.isEmpty())
        return OUString();
    const NotebookMaterial m = NotebookMaterialStore::loadMaterial(id);
    if (m.sourcePath.isEmpty())
        return OUString();
    return m.sourcePath;
}

void KqNotebookController::convertSelectedMedia(const OUString& rFormat)
{
    OUString path = mediaPathOfSelected();
    if (path.isEmpty() || !MediaTranscriptService::isMediaPath(path))
    {
        AppendChat(u"系统"_ustr,
                   u"请选中带本地路径的音视频来源，再导出格式。\n"_ustr
                       + MediaTranscriptService::supportedFormatsHint());
        return;
    }
    if (!MediaTranscriptService::hasFfmpeg())
    {
        AppendChat(u"系统"_ustr, u"格式转换需要 ffmpeg：brew install ffmpeg"_ustr);
        return;
    }
    if (m_xChatStatus)
        m_xChatStatus->set_label(u"正在转换…"_ustr);
    Application::Reschedule(true);
    const MediaConvertResult r = MediaTranscriptService::convertToFormat(path, rFormat);
    AppendChat(u"系统"_ustr, r.message);
    UpdateModelStatus();
}

void KqNotebookController::runAiOnTranscript(const OUString& rKind, const OUString& rInstruction)
{
    OUString body = MediaTranscriptService::normalizeTranscriptEdit(transcriptEditorText());
    if (body.isEmpty())
    {
        // fallback: selected material
        const OUString id = selectedSourceId();
        if (!id.isEmpty())
        {
            const NotebookMaterial m = NotebookMaterialStore::loadMaterial(id);
            body = MediaTranscriptService::normalizeTranscriptEdit(m.snippet);
        }
    }
    if (body.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"转写编辑器为空。请先「音视频转写」或「加载」来源正文。"_ustr);
        return;
    }
    // Temporarily inject as grounded context via a virtual ask using sources=body
    if (m_bBusy)
        return;
    AppendChat(u"你"_ustr, u"【"_ustr + rKind + u"】对当前转写稿处理"_ustr);
    if (m_xChatStatus)
        m_xChatStatus->set_label(u"可圈 AI · 编排处理中…"_ustr);
    m_bBusy = true;
    Application::Reschedule(true);
    OUStringBuffer prompt;
    prompt.append(u"你是可圈笔记音视频编排助手。对用户提供的转写稿进行处理。\n"
                  u"要求："_ustr
                  + rInstruction
                  + u"\n只用转写稿事实，中文 Markdown，结构清晰。\n"
                    u"===== 转写稿 =====\n"_ustr);
    if (body.getLength() > 20000)
        body = body.copy(0, 20000) + u"…"_ustr;
    prompt.append(body);
    const kqoffice::ai::StreamChatResult r
        = kqoffice::ai::streamChatCompletion(u"chat"_ustr, prompt.makeStringAndClear(), {}, {});
    m_bBusy = false;
    if (r.status == u"ok"_ustr && !r.content.isEmpty())
    {
        AppendChat(u"可圈 AI"_ustr, r.content);
        AppendStudio(rKind, rKind + u" · "_ustr + m_aProject.title, r.content);
        // also put in editor for further edit
        setTranscriptEditor(r.content);
    }
    else
        AppendChat(u"系统"_ustr, u"AI 处理失败（"_ustr + r.status + u"）。"_ustr);
    persistProject();
    UpdateModelStatus();
}

IMPL_LINK_NOARG(KqNotebookController, OnLocalAsr, weld::Button&, void)
{
    if (m_bBusy)
        return;
    const auto diag = NotebookMaterialStore::diagnoseLocalAsr();
    if (!m_xSourceList)
        return;
    const OUString id = m_xSourceList->get_selected_id();
    if (id.isEmpty())
    {
        AppendChat(u"系统"_ustr,
                   u"请先选中一条本地视频/音频来源，再点「本机转写」。\n"_ustr + diag.summary
                       + (diag.installHint.isEmpty() ? OUString()
                                                     : (u"\n"_ustr + diag.installHint)));
        return;
    }
    applyLocalAsrToMaterial(id, false);
}

IMPL_LINK_NOARG(KqNotebookController, OnTxLoad, weld::Button&, void)
{
    const OUString id = selectedSourceId();
    if (id.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"请先选中来源再「加载」。"_ustr);
        return;
    }
    sal_Int32 idx = 1;
    if (m_xSourceList)
    {
        const int n = m_xSourceList->n_children();
        for (int i = 0; i < n; ++i)
            if (m_xSourceList->get_id(i) == id)
            {
                idx = i + 1;
                break;
            }
    }
    previewSourceById(id, idx);
}

IMPL_LINK_NOARG(KqNotebookController, OnTxSave, weld::Button&, void)
{
    OUString id = m_sTranscriptMaterialId;
    if (id.isEmpty())
        id = selectedSourceId();
    if (id.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"没有绑定来源。请先选中来源并「加载」。"_ustr);
        return;
    }
    OUString body = MediaTranscriptService::normalizeTranscriptEdit(transcriptEditorText());
    if (body.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"编辑区为空，未保存。"_ustr);
        return;
    }
    const NotebookMaterial old = NotebookMaterialStore::loadMaterial(id);
    OUString header;
    if (!old.snippet.isEmpty())
    {
        const sal_Int32 cut = old.snippet.indexOf(u"\n\n---"_ustr);
        if (cut > 0)
            header = old.snippet.copy(0, cut);
        else if (old.kind == u"video"_ustr || old.kind == u"audio"_ustr)
            header = old.snippet.copy(0, std::min<sal_Int32>(old.snippet.getLength(), 400));
    }
    OUString merged;
    if (header.isEmpty())
        merged = u"[转写编辑] "_ustr + old.title + u"\n\n"_ustr + body;
    else
        merged = header + u"\n\n--- 转写编辑（用户修订）---\n"_ustr + body;
    if (!NotebookMaterialStore::replaceSnippet(id, merged))
    {
        AppendChat(u"系统"_ustr, u"保存失败。"_ustr);
        return;
    }
    m_sTranscriptMaterialId = id;
    AppendChat(u"系统"_ustr, u"已保存转写修订到来源「"_ustr + old.title + u"」。"_ustr);
    ReloadSources();
}

IMPL_LINK_NOARG(KqNotebookController, OnTxMedia, weld::Button&, void)
{
    if (m_bBusy)
        return;
    sfx2::FileDialogHelper aDlg(css::ui::dialogs::TemplateDescription::FILEOPEN_SIMPLE,
                                FileDialogFlags::NONE, m_xDialog.get());
    aDlg.SetTitle(u"可圈笔记 · 音视频转写（多格式）"_ustr);
    aDlg.AddFilter(u"音视频"_ustr,
                   u"*.mp3;*.wav;*.m4a;*.aac;*.flac;*.ogg;*.opus;*.aiff;*.wma;*.mp4;*.mov;*.mkv;"
                   u"*.webm;*.avi;*.m4v;*.mpeg;*.mpg;*.flv;*.ts"_ustr);
    aDlg.AddFilter(u"所有文件"_ustr, u"*.*"_ustr);
    if (aDlg.Execute() != ERRCODE_NONE)
        return;
    OUString path = aDlg.GetPath();
    if (path.startsWith(u"file:"_ustr))
    {
        OUString sys;
        if (osl::FileBase::getSystemPathFromFileURL(path, sys) == osl::FileBase::E_None)
            path = sys;
    }
    if (!MediaTranscriptService::isMediaPath(path))
    {
        AppendChat(u"系统"_ustr,
                   u"请选择音视频文件。\n"_ustr + MediaTranscriptService::supportedFormatsHint());
        return;
    }
    NotebookMaterial m = NotebookMaterialStore::importFile(path);
    if (m.id.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"导入失败。"_ustr);
        return;
    }
    m_aProject.materialIds.push_back(m.id);
    ReloadSources();
    persistProject();
    // select and transcribe
    if (m_xSourceList)
    {
        const int n = m_xSourceList->n_children();
        for (int i = 0; i < n; ++i)
            if (m_xSourceList->get_id(i) == m.id)
            {
                m_xSourceList->set_cursor(i);
                break;
            }
    }
    AppendChat(u"系统"_ustr, u"已导入「"_ustr + m.title + u"」，开始多格式转写…"_ustr);
    applyLocalAsrToMaterial(m.id, false);
}

IMPL_LINK_NOARG(KqNotebookController, OnTxMp3, weld::Button&, void)
{
    convertSelectedMedia(u"mp3"_ustr);
}
IMPL_LINK_NOARG(KqNotebookController, OnTxWav, weld::Button&, void)
{
    convertSelectedMedia(u"wav"_ustr);
}

IMPL_LINK_NOARG(KqNotebookController, OnTxChapters, weld::Button&, void)
{
    const OUString body = MediaTranscriptService::normalizeTranscriptEdit(transcriptEditorText());
    if (body.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"请先加载或转写正文，再「分章」。"_ustr);
        return;
    }
    // local heuristic first, then AI refine option via studio
    const OUString outline = MediaTranscriptService::buildChapterOutline(body);
    setTranscriptEditor(outline + u"\n\n--- 原文 ---\n"_ustr + body);
    AppendChat(u"系统"_ustr, u"已生成章节编排草稿（可改）。点「AI纪要」可让可圈 AI 精修。"_ustr);
    if (m_xStudioView)
        m_xStudioView->set_text(outline);
}

IMPL_LINK_NOARG(KqNotebookController, OnTxAiMinutes, weld::Button&, void)
{
    runAiOnTranscript(u"meeting-minutes"_ustr,
                      u"生成结构化会议/内容纪要：摘要、要点、决议、待办、风险；"
                      u"保留可引用原句。"_ustr);
}
IMPL_LINK_NOARG(KqNotebookController, OnTxAiPolish, weld::Button&, void)
{
    runAiOnTranscript(u"transcript-polish"_ustr,
                      u"润色转写稿：去口语赘词、补标点、分自然段，不改变事实与数字，"
                      u"保留原意；输出可直接发布的文稿。"_ustr);
}
IMPL_LINK_NOARG(KqNotebookController, OnTxAiTodo, weld::Button&, void)
{
    runAiOnTranscript(u"action-items"_ustr,
                      u"提取待办清单：任务 / 负责人(若有) / 期限(若有) / 优先级；"
                      u"无则标「未指定」。表格输出。"_ustr);
}
IMPL_LINK_NOARG(KqNotebookController, OnTxSpeak, weld::Button&, void)
{
    const OUString body = transcriptEditorText().trim();
    if (body.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"编辑区无文本可朗读。"_ustr);
        return;
    }
    AppendChat(u"系统"_ustr, LocalSpeechHub::speakPlain(body));
}

IMPL_LINK_NOARG(KqNotebookController, OnSrcAll, weld::Button&, void)
{
    if (!m_xSourceList)
        return;
    const int n = m_xSourceList->n_children();
    for (int i = 0; i < n; ++i)
        m_xSourceList->set_toggle(i, TRISTATE_TRUE);
}

IMPL_LINK_NOARG(KqNotebookController, OnSrcPreview, weld::Button&, void)
{
    if (!m_xSourceList)
        return;
    const OUString id = m_xSourceList->get_selected_id();
    if (id.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"请先选中一条来源，再点「预览」。"_ustr);
        return;
    }
    sal_Int32 idx = 1;
    const int n = m_xSourceList->n_children();
    for (int i = 0; i < n; ++i)
    {
        if (m_xSourceList->get_id(i) == id)
        {
            idx = i + 1;
            break;
        }
    }
    previewSourceById(id, idx);
}

IMPL_LINK_NOARG(KqNotebookController, OnSrcRemove, weld::Button&, void)
{
    if (!m_xSourceList)
        return;
    const OUString id = m_xSourceList->get_selected_id();
    if (id.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"请先选中要移除的来源。"_ustr);
        return;
    }
    auto& ids = m_aProject.materialIds;
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
    ReloadSources();
    AppendChat(u"系统"_ustr, u"已从当前笔记本移过来源（材料库文件仍保留）。"_ustr);
    persistProject();
}

IMPL_LINK_NOARG(KqNotebookController, OnSrcActivated, weld::TreeView&, bool)
{
    if (m_xBtnSrcPreview)
        OnSrcPreview(*m_xBtnSrcPreview);
    return true;
}

IMPL_LINK_NOARG(KqNotebookController, OnSend, weld::Button&, void)
{
    if (!m_xChatEntry)
        return;
    const OUString q = m_xChatEntry->get_text().trim();
    m_xChatEntry->set_text(OUString());
    askGrounded(q);
}

IMPL_LINK_NOARG(KqNotebookController, OnVoice, weld::Button&, void)
{
    using kqoffice::ai::chat::DocumentAIVoiceInput;
    if (m_xBtnVoice)
        m_xBtnVoice->set_label(DocumentAIVoiceInput::isListening() ? u"🎤 …"_ustr
                                                                   : u"🎤 听…"_ustr);
    if (m_xChatStatus)
        m_xChatStatus->set_label(u"语音处理中…"_ustr);
    Application::Reschedule(true);

    const auto cap = DocumentAIVoiceInput::togglePushToTalk();
    if (cap.listening)
    {
        if (m_xBtnVoice)
            m_xBtnVoice->set_label(u"⏹ 停"_ustr);
        AppendChat(u"系统"_ustr, cap.message);
        UpdateModelStatus();
        return;
    }
    if (m_xBtnVoice)
        m_xBtnVoice->set_label(u"🎤 语音"_ustr);

    if (cap.success && !cap.text.isEmpty())
    {
        if (m_xChatEntry)
        {
            OUString cur = m_xChatEntry->get_text();
            if (!cur.isEmpty() && !cur.endsWith(u" "_ustr))
                cur += u" "_ustr;
            m_xChatEntry->set_text(cur + cap.text);
        }
        AppendChat(u"系统"_ustr, cap.message + u"\n已填入输入框，确认后点发送（或 Enter）。"_ustr);
        kqoffice::ai::workbench::WorkTelemetryStore::recordSimple(u"voice"_ustr, 1);
    }
    else
    {
        AppendChat(u"系统"_ustr,
                   cap.message.isEmpty() ? u"语音未完成。"_ustr : cap.message);
        if (cap.source == u"system-dictation"_ustr)
        {
            const auto sp = LocalSpeechHub::diagnose();
            if (!sp.installHint.isEmpty())
                AppendChat(u"系统"_ustr,
                           u"本机转写能力可进一步增强：\n"_ustr + sp.installHint);
        }
    }
    UpdateModelStatus();
}

IMPL_LINK_NOARG(KqNotebookController, OnLiveTick, Timer*, void)
{
    if (!LocalSpeechHub::isLiveMeeting())
    {
        m_aLiveTick.Stop();
        return;
    }
    if (m_bBusy)
        return; // avoid overlapping STT on UI thread
    m_bBusy = true;
    OUString st;
    const OUString delta = LocalSpeechHub::pollLiveDelta(st);
    m_bBusy = false;
    if (!st.isEmpty() && m_xChatStatus)
        m_xChatStatus->set_label(st);
    if (!delta.isEmpty())
    {
        OUString cur = transcriptEditorText();
        if (!cur.isEmpty() && !cur.endsWith(u"\n"_ustr))
            cur += u"\n"_ustr;
        setTranscriptEditor(cur + delta);
        if (m_xChatStatus)
            m_xChatStatus->set_label(u"边录边出字 · 新片段已写入编辑区"_ustr);
    }
}

IMPL_LINK_NOARG(KqNotebookController, OnMeeting, weld::Button&, void)
{
    if (m_bBusy)
        return;

    // Stop live STT session
    if (LocalSpeechHub::isLiveMeeting())
    {
        m_aLiveTick.Stop();
        OUString audioPath, fullText, st;
        if (m_xChatStatus)
            m_xChatStatus->set_label(u"正在结束边录边出字…"_ustr);
        Application::Reschedule(true);
        if (!LocalSpeechHub::stopLiveMeeting(audioPath, fullText, st))
        {
            AppendChat(u"系统"_ustr, st);
            if (m_xBtnMeeting)
                m_xBtnMeeting->set_label(u"🎙 会议"_ustr);
            UpdateModelStatus();
            return;
        }
        if (m_xBtnMeeting)
            m_xBtnMeeting->set_label(u"🎙 会议"_ustr);
        // merge editor live text if richer
        OUString body = MediaTranscriptService::normalizeTranscriptEdit(transcriptEditorText());
        if (fullText.getLength() > body.getLength())
            body = fullText;
        if (body.isEmpty())
            body = fullText;
        setTranscriptEditor(body);

        NotebookMaterial m;
        if (!audioPath.isEmpty())
            m = NotebookMaterialStore::importFile(audioPath);
        if (m.id.isEmpty())
            m = NotebookMaterialStore::importTranscript(u"会议边录转写"_ustr, body,
                                                        u"subtitle"_ustr);
        else if (!body.isEmpty())
        {
            const OUString merged = u"[会议边录边出字] "_ustr + m.title + u"\n路径: "_ustr
                                    + m.sourcePath + u"\n\n--- 转写 ---\n"_ustr + body;
            NotebookMaterialStore::replaceSnippet(m.id, merged);
        }
        if (!m.id.isEmpty())
        {
            m_aProject.materialIds.push_back(m.id);
            m_sTranscriptMaterialId = m.id;
            ReloadSources();
            persistProject();
        }
        AppendChat(u"系统"_ustr, st + u"\n会议转写已入库，可点「说话人」分离，或 AI纪要。"_ustr);
        kqoffice::ai::workbench::WorkTelemetryStore::recordSimple(u"voice"_ustr, 1);
        UpdateModelStatus();
        return;
    }

    // Legacy non-live meeting still recording?
    if (LocalSpeechHub::isMeetingRecording())
    {
        OUString audioPath, st;
        if (!LocalSpeechHub::stopMeeting(audioPath, st))
        {
            AppendChat(u"系统"_ustr, st);
            if (m_xBtnMeeting)
                m_xBtnMeeting->set_label(u"🎙 会议"_ustr);
            UpdateModelStatus();
            return;
        }
        if (m_xBtnMeeting)
            m_xBtnMeeting->set_label(u"🎙 会议"_ustr);
        AppendChat(u"系统"_ustr, st + u"\n正在导入并本机转写…"_ustr);
        Application::Reschedule(true);
        NotebookMaterial m = NotebookMaterialStore::importFile(audioPath);
        if (!m.id.isEmpty())
        {
            m_aProject.materialIds.push_back(m.id);
            ReloadSources();
            persistProject();
            applyLocalAsrToMaterial(m.id, false);
        }
        UpdateModelStatus();
        return;
    }

    // Prefer live STT when tools ready
    OUString st;
    const auto asr = NotebookMaterialStore::diagnoseLocalAsr();
    if (asr.hasFfmpeg && asr.hasWhisper)
    {
        if (!LocalSpeechHub::startLiveMeeting(st, 6, 7200))
        {
            AppendChat(u"系统"_ustr, st);
            return;
        }
        if (m_xBtnMeeting)
            m_xBtnMeeting->set_label(u"⏹ 结束出字"_ustr);
        setTranscriptEditor(OUString());
        m_aLiveTick.Start();
        AppendChat(u"系统"_ustr, st);
        UpdateModelStatus();
        return;
    }

    // Fallback classic long recording
    if (!LocalSpeechHub::startMeeting(st, 7200))
    {
        AppendChat(u"系统"_ustr, st);
        const auto sp = LocalSpeechHub::diagnose();
        if (!sp.installHint.isEmpty())
            AppendChat(u"系统"_ustr, sp.installHint);
        return;
    }
    if (m_xBtnMeeting)
        m_xBtnMeeting->set_label(u"⏹ 结束会议"_ustr);
    AppendChat(u"系统"_ustr, st + u"\n（未装 whisper 时为整段录音；装好后可边录边出字）"_ustr);
    UpdateModelStatus();
}

sal_Int32 KqNotebookController::selectedSpeakerCount() const
{
    if (!m_xSpkCount)
        return 2;
    const int a = m_xSpkCount->get_active();
    if (a == 1)
        return 3;
    if (a == 2)
        return 4;
    return 2;
}

void KqNotebookController::rebuildTimelineFromEditor()
{
    m_aTimeline = MediaTranscriptService::parseSpeakerTimeline(transcriptEditorText());
    if (!m_xTimelineList)
        return;
    m_xTimelineList->clear();
    for (size_t i = 0; i < m_aTimeline.size(); ++i)
    {
        const auto& c = m_aTimeline[i];
        OUString label;
        if (c.startSec >= 0)
            label = OUString::number(static_cast<sal_Int32>(c.startSec)) + u"s · "_ustr;
        label += c.speaker;
        label += u"： "_ustr;
        OUString preview = c.text;
        if (preview.getLength() > 40)
            preview = preview.copy(0, 40) + u"…"_ustr;
        label += preview;
        m_xTimelineList->append(OUString::number(static_cast<sal_Int32>(i)), label);
    }
}

sal_Int32 KqNotebookController::selectedTimelineIndex() const
{
    if (!m_xTimelineList)
        return -1;
    const OUString id = m_xTimelineList->get_selected_id();
    if (id.isEmpty())
        return -1;
    return id.toInt32();
}

void KqNotebookController::applyTimelineCues(
    std::vector<MediaTranscriptService::TranscriptCue> aCues, sal_Int32 nSelectIndex)
{
    // re-serialize so offsets refresh via parse
    const OUString doc = MediaTranscriptService::serializeTimeline(aCues);
    setTranscriptEditor(doc);
    rebuildTimelineFromEditor();
    if (m_xStudioView)
        m_xStudioView->set_text(doc);
    if (nSelectIndex >= 0 && m_xTimelineList
        && static_cast<size_t>(nSelectIndex) < m_aTimeline.size())
    {
        m_xTimelineList->set_cursor(nSelectIndex);
        jumpToCue(nSelectIndex);
    }
}

void KqNotebookController::jumpToCue(sal_Int32 nIndex)
{
    if (nIndex < 0 || static_cast<size_t>(nIndex) >= m_aTimeline.size())
        return;
    const auto& c = m_aTimeline[static_cast<size_t>(nIndex)];
    if (m_xTranscriptEdit)
    {
        m_xTranscriptEdit->select_region(c.startChar, c.endChar);
        m_xTranscriptEdit->set_position(c.startChar);
    }
    OUString detail = u"**"_ustr + c.speaker + u"**"_ustr;
    if (c.startSec >= 0)
        detail += u" · `"_ustr + OUString::number(c.startSec) + u"s`"_ustr;
    detail += u"\n\n"_ustr + c.text;
    if (m_xStudioView)
        m_xStudioView->set_text(detail);
    if (m_xChatStatus)
    {
        OUString st = u"时间轴 · "_ustr + c.speaker;
        if (c.startSec >= 0)
            st += u" @ "_ustr + OUString::number(static_cast<sal_Int32>(c.startSec)) + u"s"_ustr;
        m_xChatStatus->set_label(st);
    }
}

IMPL_LINK_NOARG(KqNotebookController, OnTimelineChanged, weld::TreeView&, void)
{
    if (!m_xTimelineList)
        return;
    const OUString id = m_xTimelineList->get_selected_id();
    if (id.isEmpty())
        return;
    jumpToCue(id.toInt32());
}

IMPL_LINK_NOARG(KqNotebookController, OnTxRename, weld::Button&, void)
{
    OUString names;
    if (m_xSpeakerNames)
        names = m_xSpeakerNames->get_text().trim();
    if (names.isEmpty())
    {
        AppendChat(u"系统"_ustr,
                   u"请在名称框输入，例如：张三,李四 或 主持人,嘉宾,记录员"_ustr);
        return;
    }
    const OUString body = transcriptEditorText();
    if (body.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"转写区为空，请先「说话人」分离。"_ustr);
        return;
    }
    const OUString renamed = MediaTranscriptService::renameSpeakers(body, names);
    setTranscriptEditor(renamed);
    rebuildTimelineFromEditor();
    if (m_xStudioView)
        m_xStudioView->set_text(renamed);
    AppendChat(u"系统"_ustr, u"已按「"_ustr + names + u"」重命名说话人。时间轴已刷新。"_ustr);
}

IMPL_LINK_NOARG(KqNotebookController, OnTxPlayCue, weld::Button&, void)
{
    if (!m_xTimelineList || m_aTimeline.empty())
    {
        AppendChat(u"系统"_ustr, u"请先说话人分离并选中时间轴条目。"_ustr);
        return;
    }
    const OUString id = m_xTimelineList->get_selected_id();
    sal_Int32 idx = id.isEmpty() ? 0 : id.toInt32();
    if (idx < 0 || static_cast<size_t>(idx) >= m_aTimeline.size())
        idx = 0;
    jumpToCue(idx);
    const auto& c = m_aTimeline[static_cast<size_t>(idx)];
    const OUString media = mediaPathOfSelected();
    if (media.isEmpty() || c.startSec < 0)
    {
        AppendChat(u"系统"_ustr,
                   u"已定位正文。试听需要：① 选中带本地路径的音视频来源 ② 条目含时间戳。"_ustr);
        return;
    }
    // ffplay or afplay from offset
    const sal_Int32 sec = static_cast<sal_Int32>(c.startSec);
    OUString cmd;
#if defined(MACOSX) || defined(__APPLE__)
    // ffmpeg pipe to afplay for ~12s clip
    cmd = u"ffmpeg -ss "_ustr + OUString::number(sec) + u" -t 12 -i "_ustr;
    // shell quote media
    OUString q = media;
    q = q.replaceAll(u"'"_ustr, u"'\\''"_ustr);
    cmd += u"'"_ustr + q + u"' -f wav - 2>/dev/null | afplay - &"_ustr;
#else
    cmd = u"ffplay -nodisp -autoexit -ss "_ustr + OUString::number(sec) + u" -t 12 "_ustr + media
          + u" >/dev/null 2>&1 &"_ustr;
#endif
    std::system(OUStringToOString(cmd, RTL_TEXTENCODING_UTF8).getStr());
    AppendChat(u"系统"_ustr, u"试听 "_ustr + c.speaker + u" @ "_ustr + OUString::number(sec)
                               + u"s（约 12 秒）"_ustr);
}

IMPL_LINK_NOARG(KqNotebookController, OnTxDiarize, weld::Button&, void)
{
    if (m_bBusy)
        return;
    const sal_Int32 nSpk = selectedSpeakerCount();
    const OUString media = mediaPathOfSelected();
    OUString st;
    OUString result;
    if (!media.isEmpty() && MediaTranscriptService::isMediaPath(media))
    {
        if (m_xChatStatus)
            m_xChatStatus->set_label(u"说话人分离中…("_ustr + OUString::number(nSpk)
                                     + u" 人）"_ustr);
        m_bBusy = true;
        Application::Reschedule(true);
        result = MediaTranscriptService::diarizeMediaFile(
            media, st, nSpk, 900, []() { Application::Reschedule(true); });
        m_bBusy = false;
    }
    else
    {
        const OUString body
            = MediaTranscriptService::normalizeTranscriptEdit(transcriptEditorText());
        if (body.isEmpty())
        {
            AppendChat(u"系统"_ustr,
                       u"请先选中音视频来源，或在转写编辑区放入正文，再点「说话人」。\n"
                       u"可先选人数（2/3/4），分离后用「张三,李四」改名。\n"
                       u"更准：pip install whisperx（可选）"_ustr);
            return;
        }
        result = MediaTranscriptService::diarizePlainText(body, nSpk);
        st = u"说话人分离（文本启发式 · "_ustr + OUString::number(nSpk) + u" 人）"_ustr;
    }
    if (result.isEmpty())
    {
        AppendChat(u"系统"_ustr, st.isEmpty() ? u"说话人分离失败。"_ustr : st);
        UpdateModelStatus();
        return;
    }
    // auto-apply names if entry filled
    if (m_xSpeakerNames)
    {
        const OUString names = m_xSpeakerNames->get_text().trim();
        if (!names.isEmpty())
            result = MediaTranscriptService::renameSpeakers(result, names);
    }
    setTranscriptEditor(result);
    rebuildTimelineFromEditor();
    if (m_xStudioView)
        m_xStudioView->set_text(result);
    AppendStudio(u"diarization"_ustr, u"说话人分离 · "_ustr + m_aProject.title, result);
    // auto-merge adjacent same speaker for cleaner timeline
    {
        auto cues = MediaTranscriptService::parseSpeakerTimeline(result);
        const auto merged = MediaTranscriptService::mergeAdjacentSameSpeaker(cues);
        if (merged.size() < cues.size())
        {
            result = MediaTranscriptService::serializeTimeline(merged);
            setTranscriptEditor(result);
            rebuildTimelineFromEditor();
        }
    }
    AppendChat(u"系统"_ustr,
               st + u"\n已生成时间轴（"_ustr
                   + OUString::number(static_cast<sal_Int32>(m_aTimeline.size()))
                   + u" 条）。可：改名 / 换人 / 并同人 / 并下条 / ↑↓ / 删条 / 试听。"_ustr);
    UpdateModelStatus();
}

IMPL_LINK_NOARG(KqNotebookController, OnTxMergeAdj, weld::Button&, void)
{
    auto cues = MediaTranscriptService::parseSpeakerTimeline(transcriptEditorText());
    if (cues.empty())
    {
        AppendChat(u"系统"_ustr, u"没有可合并的时间轴。请先「说话人」。"_ustr);
        return;
    }
    const size_t before = cues.size();
    cues = MediaTranscriptService::mergeAdjacentSameSpeaker(cues);
    applyTimelineCues(std::move(cues));
    AppendChat(u"系统"_ustr, u"已合并相邻同说话人："_ustr + OUString::number(static_cast<sal_Int32>(before))
                               + u" → "_ustr
                               + OUString::number(static_cast<sal_Int32>(m_aTimeline.size()))
                               + u" 条。"_ustr);
}

IMPL_LINK_NOARG(KqNotebookController, OnTxMergeNext, weld::Button&, void)
{
    const sal_Int32 idx = selectedTimelineIndex();
    if (idx < 0)
    {
        AppendChat(u"系统"_ustr, u"请先在时间轴选中一条，再「并下条」。"_ustr);
        return;
    }
    auto cues = MediaTranscriptService::parseSpeakerTimeline(transcriptEditorText());
    cues = MediaTranscriptService::mergeCueWithNext(std::move(cues), idx);
    applyTimelineCues(std::move(cues), idx);
    AppendChat(u"系统"_ustr, u"已与下一条合并。"_ustr);
}

IMPL_LINK_NOARG(KqNotebookController, OnTxCycleSpk, weld::Button&, void)
{
    const sal_Int32 idx = selectedTimelineIndex();
    if (idx < 0)
    {
        AppendChat(u"系统"_ustr, u"请先选中时间轴条目，再「换人」。"_ustr);
        return;
    }
    auto cues = MediaTranscriptService::parseSpeakerTimeline(transcriptEditorText());
    if (static_cast<size_t>(idx) >= cues.size())
        return;

    OUString nextName;
    if (m_xSpeakerNames)
    {
        const OUString raw = m_xSpeakerNames->get_text().trim();
        if (!raw.isEmpty())
        {
            // split names like renameSpeakers
            std::vector<OUString> names;
            OUStringBuffer cur;
            auto flush = [&]() {
                const OUString t = cur.makeStringAndClear().trim();
                if (!t.isEmpty())
                    names.push_back(t);
            };
            for (sal_Int32 i = 0; i < raw.getLength(); ++i)
            {
                const sal_Unicode c = raw[i];
                if (c == u',' || c == u'，' || c == u'、' || c == u';' || c == u'；' || c == u'/'
                    || c == u'|')
                    flush();
                else
                    cur.append(c);
            }
            flush();
            if (!names.empty())
            {
                size_t pos = 0;
                const OUString& curSp = cues[static_cast<size_t>(idx)].speaker;
                for (size_t i = 0; i < names.size(); ++i)
                {
                    if (names[i] == curSp)
                    {
                        pos = i;
                        break;
                    }
                }
                nextName = names[(pos + 1) % names.size()];
            }
        }
    }
    cues = MediaTranscriptService::reassignCueSpeaker(std::move(cues), idx, nextName,
                                                      selectedSpeakerCount());
    applyTimelineCues(std::move(cues), idx);
    AppendChat(u"系统"_ustr, u"已切换说话人 → "_ustr + m_aTimeline[static_cast<size_t>(idx)].speaker);
}

IMPL_LINK_NOARG(KqNotebookController, OnTxDelCue, weld::Button&, void)
{
    const sal_Int32 idx = selectedTimelineIndex();
    if (idx < 0)
    {
        AppendChat(u"系统"_ustr, u"请选中要删除的时间轴条目。"_ustr);
        return;
    }
    auto cues = MediaTranscriptService::parseSpeakerTimeline(transcriptEditorText());
    cues = MediaTranscriptService::deleteCue(std::move(cues), idx);
    const sal_Int32 sel = std::min(idx, static_cast<sal_Int32>(cues.size()) - 1);
    applyTimelineCues(std::move(cues), sel >= 0 ? sel : -1);
    AppendChat(u"系统"_ustr, u"已删除该轮次。"_ustr);
}

IMPL_LINK_NOARG(KqNotebookController, OnTxUp, weld::Button&, void)
{
    const sal_Int32 idx = selectedTimelineIndex();
    if (idx <= 0)
        return;
    auto cues = MediaTranscriptService::parseSpeakerTimeline(transcriptEditorText());
    cues = MediaTranscriptService::moveCue(std::move(cues), idx, -1);
    applyTimelineCues(std::move(cues), idx - 1);
}

IMPL_LINK_NOARG(KqNotebookController, OnTxDown, weld::Button&, void)
{
    const sal_Int32 idx = selectedTimelineIndex();
    if (idx < 0)
        return;
    auto cues = MediaTranscriptService::parseSpeakerTimeline(transcriptEditorText());
    if (static_cast<size_t>(idx) + 1 >= cues.size())
        return;
    cues = MediaTranscriptService::moveCue(std::move(cues), idx, +1);
    applyTimelineCues(std::move(cues), idx + 1);
}

IMPL_LINK_NOARG(KqNotebookController, OnTtsReply, weld::Button&, void)
{
    // Prefer last assistant chat turn; else Studio body
    OUString text;
    for (auto it = m_aProject.chat.rbegin(); it != m_aProject.chat.rend(); ++it)
    {
        if (it->role == u"assistant"_ustr && !it->text.isEmpty())
        {
            text = it->text;
            break;
        }
    }
    if (text.isEmpty() && !m_sStudioArtifact.isEmpty())
        text = m_sStudioArtifact;
    if (text.isEmpty() && m_xStudioView)
        text = m_xStudioView->get_text();
    if (text.isEmpty())
    {
        AppendChat(u"系统"_ustr,
                   u"没有可朗读内容。请先问答或生成 Studio 制品，再点「朗读」。"_ustr);
        return;
    }
    AppendChat(u"系统"_ustr, LocalSpeechHub::speakPlain(text));
}

IMPL_LINK_NOARG(KqNotebookController, OnEntryActivate, weld::Entry&, bool)
{
    if (m_xBtnSend)
        OnSend(*m_xBtnSend);
    return true;
}

IMPL_LINK_NOARG(KqNotebookController, OnChipSum, weld::Button&, void)
{
    askGrounded(u"请总结所有勾选来源的核心内容，分点列出并标注引用。"_ustr);
}
IMPL_LINK_NOARG(KqNotebookController, OnChipKey, weld::Button&, void)
{
    askGrounded(u"提取关键要点、定义与结论，用项目符号列出并引用。"_ustr);
}
IMPL_LINK_NOARG(KqNotebookController, OnChipCmp, weld::Button&, void)
{
    askGrounded(u"对比各勾选来源的异同与互补，分点说明并引用。"_ustr);
}
IMPL_LINK_NOARG(KqNotebookController, OnChipFaq, weld::Button&, void)
{
    askGrounded(u"根据来源生成 8 个常见问题及简要答案，并引用。"_ustr);
}

IMPL_LINK_NOARG(KqNotebookController, OnAudio, weld::Button&, void)
{
    runStudio(u"audio-script"_ustr,
              kqoffice::ai::notebook::NotebookStudioPipeline::audioOverviewInstruction());
}
IMPL_LINK_NOARG(KqNotebookController, OnVideo, weld::Button&, void)
{
    runStudio(u"video-overview"_ustr,
              kqoffice::ai::notebook::NotebookStudioPipeline::videoOverviewInstruction());
}
IMPL_LINK_NOARG(KqNotebookController, OnGuide, weld::Button&, void)
{
    runStudio(u"guide"_ustr,
              u"产出学习指南：目标、核心概念、要点清单、自测题、延伸思考。"_ustr);
}
IMPL_LINK_NOARG(KqNotebookController, OnBrief, weld::Button&, void)
{
    runStudio(u"brief"_ustr, u"一页纸简报：背景、要点、风险/机会、建议行动。"_ustr);
}
IMPL_LINK_NOARG(KqNotebookController, OnStudioFaq, weld::Button&, void)
{
    runStudio(u"faq"_ustr, u"生成 10 组 FAQ，答案简洁并带来源引用。"_ustr);
}
IMPL_LINK_NOARG(KqNotebookController, OnTimeline, weld::Button&, void)
{
    runStudio(u"timeline"_ustr,
              u"按时间或逻辑阶段排列事件/主题，并说明依据与引用。"_ustr);
}

IMPL_LINK_NOARG(KqNotebookController, OnFlashcards, weld::Button&, void)
{
    runStudio(u"flashcards"_ustr,
              u"生成 12 张闪卡。每张格式：\n### 卡片 N\n**Q:** 问题\n**A:** 答案（含 [编号] 引用）\n"
              u"覆盖定义、事实、对比、易错点。"_ustr);
}

IMPL_LINK_NOARG(KqNotebookController, OnMindmap, weld::Button&, void)
{
    runStudio(u"mindmap"_ustr,
              u"输出 Markdown 思维导图：先给一个中心主题，再用层级列表（- / 缩进子项）展开分支；"
              u"并附一段 mermaid mindmap 代码块。节点旁标注 [引用编号]。"_ustr);
}

IMPL_LINK_NOARG(KqNotebookController, OnSaveNote, weld::Button&, void)
{
    if (m_sStudioArtifact.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"Studio 尚无制品。请先生成学习指南/音频脚本等。"_ustr);
        return;
    }
    NotebookNote n = LocalNotebookStore::createNote(u"可圈笔记 · "_ustr + m_sStudioKind);
    n.body = m_sStudioArtifact;
    LocalNotebookStore::saveNote(n);
    AppendChat(u"系统"_ustr, u"已保存为本地笔记："_ustr + n.title);
}

IMPL_LINK_NOARG(KqNotebookController, OnSpeak, weld::Button&, void)
{
    if (m_sStudioArtifact.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"请先生成「音频概览」脚本，再点双声朗读。"_ustr);
        return;
    }
    AppendChat(u"系统"_ustr, speakScriptMacDual(m_sStudioArtifact, false));
}

OUString KqNotebookController::currentArtifactBody() const
{
    if (m_xArtifactList)
    {
        const OUString id = m_xArtifactList->get_selected_id();
        if (!id.isEmpty())
        {
            for (const auto& a : m_aProject.artifacts)
            {
                if (a.id == id && !a.body.isEmpty())
                    return a.body;
            }
        }
    }
    if (!m_sStudioArtifact.isEmpty())
        return m_sStudioArtifact;
    if (m_xStudioView)
    {
        const OUString t = m_xStudioView->get_text().trim();
        if (!t.isEmpty())
            return t;
    }
    return OUString();
}

OUString KqNotebookController::currentArtifactTitle() const
{
    if (m_xArtifactList)
    {
        const int row = m_xArtifactList->get_selected_index();
        if (row >= 0)
            return m_xArtifactList->get_text(row);
    }
    if (!m_sStudioKind.isEmpty())
        return u"可圈笔记 · "_ustr + m_sStudioKind;
    return u"可圈笔记制品"_ustr;
}

bool KqNotebookController::confirmMaterialize(const OUString& rTargetZh, const OUString& rPreview)
{
    OUString preview = rPreview;
    if (preview.getLength() > 900)
        preview = preview.copy(0, 900) + u"…"_ustr;
    const OUString msg = u"即将把当前 Studio 制品写入「"_ustr + rTargetZh
                         + u"」。\n\n预览：\n"_ustr + preview
                         + u"\n\n确认后写入（可用撤销）。不会静默上传。"_ustr;
    std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(
        m_xDialog.get(), VclMessageType::Question, VclButtonsType::OkCancel, msg));
    xBox->set_default_response(RET_CANCEL);
    return xBox->run() == RET_OK;
}

bool KqNotebookController::openFactoryDoc(const OUString& rFactoryUrl)
{
    try
    {
        auto xContext = ::comphelper::getProcessComponentContext();
        if (!xContext.is())
            return false;
        css::uno::Reference<css::frame::XDesktop2> xDesktop
            = css::frame::Desktop::create(xContext);
        if (!xDesktop.is())
            return false;
        css::uno::Reference<css::lang::XComponent> xComp = xDesktop->loadComponentFromURL(
            rFactoryUrl, u"_default"_ustr, 0, css::uno::Sequence<css::beans::PropertyValue>());
        return xComp.is();
    }
    catch (...)
    {
        return false;
    }
}

bool KqNotebookController::materializeToWriter(const OUString& rBody, const OUString& rTitle)
{
    if (rBody.isEmpty())
        return false;
    // Ensure a Writer is active so DocumentAIApply / DiffApplier target the right surface.
    if (!openFactoryDoc(u"private:factory/swriter"_ustr))
    {
        AppendChat(u"系统"_ustr, u"无法打开文字文档。"_ustr);
        return false;
    }
    Application::Reschedule(true);

    kqoffice::ai::chat::ApplyPlan plan;
    plan.planId = u"ap-notebook-to-writer"_ustr;
    plan.rawOutput = rBody;
    kqoffice::ai::chat::DiffOperation op;
    op.opType = u"insert"_ustr;
    op.target = u"para:1"_ustr;
    // Title + body for readable doc
    OUStringBuffer body;
    if (!rTitle.isEmpty())
    {
        body.append(rTitle);
        body.append(u"\n\n"_ustr);
    }
    body.append(rBody);
    op.newText = body.makeStringAndClear();
    plan.operations.push_back(op);

    // Fresh doc: mark snapshot so stale guard does not block first apply.
    {
        const OUString snap = kqoffice::ai::chat::DocumentAIDocumentTools::computeSnapshotHash();
        if (!snap.isEmpty())
            kqoffice::ai::chat::DocumentAIDocumentTools::markSeen(snap);
    }

    const auto result = kqoffice::ai::chat::DocumentAIApply::applyApproved(plan);
    if (!result.success)
    {
        AppendChat(u"系统"_ustr,
                   u"写入文字失败："_ustr
                       + kqoffice::ai::chat::DocumentAIApply::userFacingErrorZh(
                           result.error, result.engine, result.surface));
        return false;
    }
    AppendChat(u"系统"_ustr,
               u"已写入文字 · "_ustr
                   + kqoffice::ai::chat::DocumentAIApply::userFacingEngineZh(result.engine)
                   + u" · 可用撤销（Ctrl/Cmd+Z）"_ustr);
    return true;
}

bool KqNotebookController::materializeToImpress(const OUString& rBody, const OUString& rTitle)
{
    if (rBody.isEmpty())
        return false;
    if (!openFactoryDoc(u"private:factory/simpress"_ustr))
    {
        AppendChat(u"系统"_ustr, u"无法打开演示文档。"_ustr);
        return false;
    }
    Application::Reschedule(true);

    OUString source = rBody;
    // Prefer multi-slide outline; otherwise wrap as a controlled single/multi page draft.
    kqoffice::ai::chat::ApplyPlan plan;
    if (kqoffice::ai::chat::AgentChatDiffExtractor::looksLikeOutlineSlideContent(source))
        plan = kqoffice::ai::chat::AgentChatDiffExtractor::extractOutlineSlidePlan(source);
    if (!kqoffice::ai::chat::AgentChatDiffExtractor::validate(plan))
    {
        // Build a minimal controlled outline from paragraphs / bullets.
        OUStringBuffer outline;
        outline.append(u"## 1. "_ustr);
        outline.append(rTitle.isEmpty() ? u"笔记概览"_ustr : rTitle);
        outline.append(u"\n"_ustr);
        sal_Int32 pos = 0;
        sal_Int32 slide = 1;
        sal_Int32 linesOnSlide = 0;
        while (pos < source.getLength() && slide <= 12)
        {
            sal_Int32 nl = source.indexOf(u'\n', pos);
            if (nl < 0)
                nl = source.getLength();
            OUString line = source.copy(pos, nl - pos).trim();
            pos = nl + 1;
            if (line.isEmpty())
                continue;
            if (line.startsWith(u"#"_ustr))
            {
                ++slide;
                linesOnSlide = 0;
                while (!line.isEmpty() && line[0] == u'#')
                    line = line.copy(1);
                line = line.trim();
                outline.append(u"\n## "_ustr);
                outline.append(OUString::number(slide));
                outline.append(u". "_ustr);
                outline.append(line.isEmpty() ? u"续"_ustr : line);
                outline.append(u"\n"_ustr);
                continue;
            }
            if (linesOnSlide >= 6)
            {
                ++slide;
                linesOnSlide = 0;
                outline.append(u"\n## "_ustr);
                outline.append(OUString::number(slide));
                outline.append(u". 续\n"_ustr);
            }
            if (!line.startsWith(u"-"_ustr) && !line.startsWith(u"*"_ustr))
                outline.append(u"- "_ustr);
            outline.append(line);
            outline.append(u"\n"_ustr);
            ++linesOnSlide;
        }
        plan = kqoffice::ai::chat::AgentChatDiffExtractor::extractOutlineSlidePlan(
            outline.makeStringAndClear());
    }
    if (!kqoffice::ai::chat::AgentChatDiffExtractor::validate(plan))
    {
        AppendChat(u"系统"_ustr, u"无法从制品生成受控大纲页；请先用 Studio 生成简报/指南。"_ustr);
        return false;
    }
    plan.planId = u"ap-notebook-to-impress"_ustr;
    plan.rawOutput = rBody;

    {
        const OUString snap = kqoffice::ai::chat::DocumentAIDocumentTools::computeSnapshotHash();
        if (!snap.isEmpty())
            kqoffice::ai::chat::DocumentAIDocumentTools::markSeen(snap);
    }

    const auto result = kqoffice::ai::chat::DocumentAIApply::applyApproved(plan);
    if (!result.success)
    {
        AppendChat(u"系统"_ustr,
                   u"写入演示失败："_ustr
                       + kqoffice::ai::chat::DocumentAIApply::userFacingErrorZh(
                           result.error, result.engine, result.surface));
        return false;
    }
    AppendChat(u"系统"_ustr,
               u"已按受控大纲写入演示 · 页数约 "_ustr
                   + OUString::number(static_cast<sal_Int32>(plan.operations.size()))
                   + u" · "_ustr
                   + kqoffice::ai::chat::DocumentAIApply::userFacingEngineZh(result.engine)
                   + u" · 可撤销"_ustr);
    return true;
}

IMPL_LINK_NOARG(KqNotebookController, OnToWriter, weld::Button&, void)
{
    const OUString body = currentArtifactBody();
    if (body.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"请先在 Studio 生成制品（简报/指南等），或选中制品库条目。"_ustr);
        return;
    }
    const OUString title = currentArtifactTitle();
    if (!confirmMaterialize(u"文字 Writer"_ustr, body))
    {
        AppendChat(u"系统"_ustr, u"已取消写入文字 · 主文档未改"_ustr);
        return;
    }
    materializeToWriter(body, title);
}

IMPL_LINK_NOARG(KqNotebookController, OnToImpress, weld::Button&, void)
{
    const OUString body = currentArtifactBody();
    if (body.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"请先在 Studio 生成制品，或选中制品库条目。"_ustr);
        return;
    }
    const OUString title = currentArtifactTitle();
    if (!confirmMaterialize(u"演示 Impress（受控大纲）"_ustr, body))
    {
        AppendChat(u"系统"_ustr, u"已取消写入演示 · 主文档未改"_ustr);
        return;
    }
    materializeToImpress(body, title);
}

IMPL_LINK_NOARG(KqNotebookController, OnExportAudio, weld::Button&, void)
{
    const OUString body = currentArtifactBody();
    if (body.isEmpty())
    {
        AppendChat(u"系统"_ustr, u"请先生成「音频概览」脚本，再导出音频/成品包。"_ustr);
        return;
    }
    // Normalize script into Studio view for product feel.
    const OUString normalized
        = kqoffice::ai::notebook::NotebookStudioPipeline::normalizeDualVoiceScript(body);
    if (m_xStudioView)
        m_xStudioView->set_text(normalized);
    m_sStudioArtifact = normalized;
    m_sStudioKind = m_sStudioKind.isEmpty() ? u"audio-script"_ustr : m_sStudioKind;

    AppendChat(u"系统"_ustr, speakScriptMacDual(normalized, true));

    // Always also write a local 成品包 (script + README + dual shells).
    OUString sourcesIdx;
    for (const auto& mid : m_aProject.materialIds)
    {
        const NotebookMaterial m = NotebookMaterialStore::loadMaterial(mid);
        if (m.id.isEmpty())
            continue;
        sourcesIdx += m.title + u"\t"_ustr + m.kind + u"\n"_ustr;
    }
    AppendChat(u"系统"_ustr,
               exportStudioProductPackage(m_aProject.title, m_sStudioKind, normalized, sourcesIdx));
}

std::shared_ptr<KqNotebookController> g_pNotebook;

bool acceptVoiceIntoNotebook(const OUString& rText)
{
    if (rText.isEmpty() || !g_pNotebook)
        return false;
    weld::Window* pDlg = g_pNotebook->getDialog();
    if (!pDlg || !pDlg->get_visible())
        return false;
    // Fill chat entry via controller public method
    g_pNotebook->AcceptVoiceText(rText);
    pDlg->present();
    return true;
}

} // namespace

KqNotebookDispatcher& KqNotebookDispatcher::Get()
{
    static KqNotebookDispatcher inst;
    return inst;
}

void KqNotebookDispatcher::Show(weld::Window* pParent)
{
    if (!pParent)
        pParent = Application::GetDefDialogParent();
    if (g_pNotebook && g_pNotebook->getDialog() && g_pNotebook->getDialog()->get_visible())
    {
        g_pNotebook->getDialog()->present();
        return;
    }
    g_pNotebook = std::make_shared<KqNotebookController>(pParent);
    g_pNotebook->getDialog()->show();
}

bool KqNotebookDispatcher::TryAcceptVoiceText(const OUString& rText)
{
    return acceptVoiceIntoNotebook(rText);
}

} // namespace sfx2

// C ABI for dock / tray / ObjC bridge (same pattern as voice/screenshot).
extern "C" void kqoffice_show_notebook()
{
    sfx2::KqNotebookDispatcher::Get().Show(Application::GetDefDialogParent());
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
