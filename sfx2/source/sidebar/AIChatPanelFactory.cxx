/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: In-app AI chat).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatPanel.hxx"
#include "AIChatShellPanel.hxx"

#include <comphelper/compbase.hxx>
#include <comphelper/namedvaluecollection.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <sfx2/sidebar/SidebarPanelBase.hxx>
#include <vcl/weld/weldutils.hxx>

#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/ui/XUIElementFactory.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>

#include <cstdlib>

using namespace css;
using namespace css::uno;

namespace sfx2::sidebar
{
namespace
{
bool g_bPreferFullAfterShell = false;
}

void RequestFullAIChatPanelNext() { g_bPreferFullAfterShell = true; }

bool PreferFullAIChatPanel()
{
    // Force shell-only for GUI stability testing.
    if (const char* shellOnly = std::getenv("KQ_AICHAT_SHELL_ONLY");
        shellOnly && shellOnly[0] == '1' && shellOnly[1] == '\0')
        return false;
    // Immediate full panel (skip shell).
    if (const char* full = std::getenv("KQ_AICHAT_FULL");
        full && full[0] == '1' && full[1] == '\0')
        return true;
    return g_bPreferFullAfterShell;
}
} // namespace sfx2::sidebar

namespace
{

using AIChatPanelFactoryInterfaceBase
    = comphelper::WeakComponentImplHelper<css::ui::XUIElementFactory,
                                          css::lang::XServiceInfo>;

class AIChatPanelFactory final : public AIChatPanelFactoryInterfaceBase
{
public:
    AIChatPanelFactory() = default;
    AIChatPanelFactory(const AIChatPanelFactory&) = delete;
    AIChatPanelFactory& operator=(const AIChatPanelFactory&) = delete;

    Reference<ui::XUIElement> SAL_CALL createUIElement(
        const OUString& rResourceURL,
        const Sequence<beans::PropertyValue>& rArguments) override;

    OUString SAL_CALL getImplementationName() override
    {
        return u"org.kqoffice.comp.sfx2.sidebar.AIChatPanelFactory"_ustr;
    }

    sal_Bool SAL_CALL supportsService(OUString const& rServiceName) override
    {
        return cppu::supportsService(this, rServiceName);
    }

    Sequence<OUString> SAL_CALL getSupportedServiceNames() override
    {
        return { u"com.sun.star.ui.UIElementFactory"_ustr };
    }
};

Reference<ui::XUIElement> SAL_CALL AIChatPanelFactory::createUIElement(
    const OUString& rResourceURL,
    const Sequence<beans::PropertyValue>& rArguments)
{
    const comphelper::NamedValueCollection aArguments(rArguments);
    Reference<frame::XFrame> xFrame(sfx2::sidebar::GetFrame(aArguments));
    Reference<awt::XWindow> xParentWindow(
        aArguments.getOrDefault(u"ParentWindow"_ustr, Reference<awt::XWindow>()));

    weld::Widget* pParent = nullptr;
    if (weld::TransportAsXWindow* pTunnel
        = dynamic_cast<weld::TransportAsXWindow*>(xParentWindow.get()))
    {
        pParent = pTunnel->getWidget();
    }

    if (!pParent)
        throw RuntimeException(u"AIChatPanelFactory::createUIElement called without ParentWindow"_ustr,
                               nullptr);
    if (!xFrame.is())
        throw RuntimeException(u"AIChatPanelFactory::createUIElement called without Frame"_ustr,
                               nullptr);

    if (!rResourceURL.endsWith(u"/AIChatPanel"_ustr))
        return Reference<ui::XUIElement>();

    // Stage1: open shell first (safe Show). Idle upgrade sets PreferFull and recreates.
    std::unique_ptr<PanelLayout> xPanel;
    if (sfx2::sidebar::PreferFullAIChatPanel())
        xPanel = std::make_unique<sfx2::sidebar::AIChatPanel>(pParent);
    else
        xPanel = std::make_unique<sfx2::sidebar::AIChatShellPanel>(pParent);

    return sfx2::sidebar::SidebarPanelBase::Create(rResourceURL, xFrame, std::move(xPanel),
                                                   ui::LayoutSize(0, -1, -1));
}

} // namespace

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface*
org_kqoffice_comp_sfx2_sidebar_AIChatPanelFactory_get_implementation(
    css::uno::XComponentContext*, css::uno::Sequence<css::uno::Any> const&)
{
    return cppu::acquire(new AIChatPanelFactory);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
