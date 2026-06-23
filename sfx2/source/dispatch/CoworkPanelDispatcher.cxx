/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * V2 W5 Day-1 — cowork dialog hook; cui registers ShowCoworkDialog at load.
 */

#include <dispatch/CoworkPanelDispatcher.hxx>

#include <sal/log.hxx>
#include <vcl/weld/weld.hxx>

namespace sfx2
{
namespace
{
CoworkPanelShowFn g_pShowPanelHook = nullptr;
}

CoworkPanelDispatcher& CoworkPanelDispatcher::Get()
{
    static CoworkPanelDispatcher aInstance;
    return aInstance;
}

void CoworkPanelDispatcher::RegisterShowPanelHook(CoworkPanelShowFn fn)
{
    g_pShowPanelHook = fn;
}

void CoworkPanelDispatcher::ShowPanel(weld::Widget* pParent)
{
    if (g_pShowPanelHook)
    {
        g_pShowPanelHook(pParent);
        return;
    }
    SAL_WARN("sfx.cowork", "CoworkPanelDispatcher: no UI hook registered");
}

} // namespace sfx2

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */