/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <WorkTelemetryHub.hxx>

#include <WorkTelemetryStore.hxx>

#include <sfx2/app.hxx>
#include <sfx2/event.hxx>
#include <sfx2/objsh.hxx>
#include <sfx2/docfile.hxx>
#include <tools/urlobj.hxx>
#include <sal/log.hxx>
#include <rtl/ustring.hxx>

using namespace kqoffice::ai::workbench;

namespace sfx2
{
namespace
{
OUString shellTitle(SfxObjectShell* pShell)
{
    if (!pShell)
        return OUString();
    OUString t = pShell->GetTitle(SFX_TITLE_DETECT);
    if (t.isEmpty())
        t = pShell->GetTitle();
    return t;
}

OUString shellIdentity(SfxObjectShell* pShell)
{
    if (!pShell)
        return OUString();
    if (SfxMedium* pMed = pShell->GetMedium())
    {
        const OUString url
            = pMed->GetURLObject().GetMainURL(INetURLObject::DecodeMechanism::NONE);
        if (!url.isEmpty())
            return url;
    }
    return u"unsaved:"_ustr + OUString::number(reinterpret_cast<sal_uInt64>(pShell));
}
} // namespace

WorkTelemetryHub& WorkTelemetryHub::Get()
{
    static WorkTelemetryHub a;
    return a;
}

WorkTelemetryHub::WorkTelemetryHub()
    : m_aSessionTick("WorkTelemetrySessionTick")
{
    m_aSessionTick.SetTimeout(60 * 1000); // 1 minute
    m_aSessionTick.SetInvokeHandler(LINK(this, WorkTelemetryHub, OnSessionTick));
}

void WorkTelemetryHub::EnsureStarted(SfxApplication& rApp)
{
    if (m_bStarted)
        return;
    m_bStarted = true;
    StartListening(rApp);
    m_aSessionTick.Start();
    // Count first minute of session
    WorkTelemetryStore::recordSessionTick(1);
    // Weekend archive nudge + week goal reached (local notification, once/day each)
    WorkTelemetryStore::maybeWeeklySystemReminder();
    WorkTelemetryStore::maybeWeekGoalReachedReminder();
    SAL_INFO("sfx.appl", "WorkTelemetryHub started (session tick + doc events)");
}

void WorkTelemetryHub::Notify(SfxBroadcaster& /*rBC*/, const SfxHint& rHint)
{
    if (rHint.GetId() != SfxHintId::ThisIsAnSfxEventHint)
        return;
    const auto* pEv = dynamic_cast<const SfxEventHint*>(&rHint);
    if (!pEv)
        return;

    SfxObjectShell* pShell = pEv->GetObjShell().get();
    const OUString title = shellTitle(pShell);
    const OUString id = shellIdentity(pShell);

    switch (pEv->GetEventId())
    {
        case SfxEventHintId::OpenDoc:
        case SfxEventHintId::LoadFinished:
            WorkTelemetryStore::recordDoc(u"open"_ustr, title, id);
            break;
        case SfxEventHintId::CreateDoc:
        case SfxEventHintId::DocCreated:
            WorkTelemetryStore::recordDoc(u"new"_ustr, title, id);
            break;
        case SfxEventHintId::SaveDocDone:
        case SfxEventHintId::SaveAsDocDone:
        case SfxEventHintId::SaveToDocDone:
            WorkTelemetryStore::recordDoc(u"save"_ustr, title, id);
            break;
        default:
            break;
    }
}

IMPL_LINK_NOARG(WorkTelemetryHub, OnSessionTick, Timer*, void)
{
    // Only tick if application still up
    if (SfxApplication* pApp = SfxGetpApp())
    {
        (void)pApp;
        WorkTelemetryStore::recordSessionTick(1);
        // Cheap: self-throttled to once per day inside store
        WorkTelemetryStore::maybeWeeklySystemReminder();
        WorkTelemetryStore::maybeWeekGoalReachedReminder();
    }
}

void WorkTelemetryHub::RecordTestPass(const OUString& rSuite, sal_Int32 nCount)
{
    WorkTelemetryEvent e;
    e.kind = u"test_pass"_ustr;
    e.count = nCount > 0 ? nCount : 1;
    e.status = rSuite;
    WorkTelemetryStore::record(e);
}

} // namespace sfx2

// C ABI for shell test gates
extern "C" SAL_DLLPUBLIC_EXPORT void kqoffice_work_record_test_pass(const char* pSuite,
                                                                     int nCount)
{
    const OUString suite
        = (pSuite && *pSuite) ? OUString::fromUtf8(pSuite) : u"unspecified"_ustr;
    sfx2::WorkTelemetryHub::RecordTestPass(suite, nCount > 0 ? nCount : 1);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
