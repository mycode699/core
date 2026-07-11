/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Workbench insights hub: session heartbeat + doc open/save/new events.
 */
#pragma once

#include <rtl/ustring.hxx>
#include <sfx2/dllapi.h>
#include <svl/lstner.hxx>
#include <vcl/timer.hxx>

class SfxApplication;

namespace sfx2
{
/// Process-wide local telemetry collector (never throws / never blocks UX hard).
class SFX2_DLLPUBLIC WorkTelemetryHub final : public SfxListener
{
public:
    static WorkTelemetryHub& Get();
    /// Idempotent: start listening + 60s session tick.
    void EnsureStarted(SfxApplication& rApp);

    /// Scripts / gates: record test_pass events.
    static void RecordTestPass(const OUString& rSuite, sal_Int32 nCount = 1);

private:
    WorkTelemetryHub();
    virtual void Notify(SfxBroadcaster& rBC, const SfxHint& rHint) override;
    DECL_LINK(OnSessionTick, Timer*, void);

    Timer m_aSessionTick;
    bool m_bStarted = false;
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
