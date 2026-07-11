/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Sidebar factory for Work Dashboard + Local Notebook panels.
 */

#include "WorkDashboardPanel.hxx"
#include "LocalNotebookPanel.hxx"

#include <comphelper/compbase.hxx>
#include <comphelper/namedvaluecollection.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <sfx2/sidebar/SidebarPanelBase.hxx>
#include <vcl/weld/weldutils.hxx>

#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/ui/XUIElementFactory.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>

using namespace css;
using namespace css::uno;

namespace
{
using WorkbenchPanelFactoryInterfaceBase
    = comphelper::WeakComponentImplHelper<css::ui::XUIElementFactory, css::lang::XServiceInfo>;

class WorkbenchPanelFactory final : public WorkbenchPanelFactoryInterfaceBase
{
public:
    WorkbenchPanelFactory() = default;

    Reference<ui::XUIElement> SAL_CALL
    createUIElement(const OUString& rResourceURL,
                    const Sequence<beans::PropertyValue>& rArguments) override;

    OUString SAL_CALL getImplementationName() override
    {
        return u"org.kqoffice.comp.sfx2.sidebar.WorkbenchPanelFactory"_ustr;
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

Reference<ui::XUIElement> SAL_CALL WorkbenchPanelFactory::createUIElement(
    const OUString& rResourceURL, const Sequence<beans::PropertyValue>& rArguments)
{
    const comphelper::NamedValueCollection aArguments(rArguments);
    Reference<frame::XFrame> xFrame(sfx2::sidebar::GetFrame(aArguments));
    Reference<awt::XWindow> xParentWindow(
        aArguments.getOrDefault(u"ParentWindow"_ustr, Reference<awt::XWindow>()));

    weld::Widget* pParent = nullptr;
    if (weld::TransportAsXWindow* pTunnel
        = dynamic_cast<weld::TransportAsXWindow*>(xParentWindow.get()))
        pParent = pTunnel->getWidget();

    if (!pParent)
        throw RuntimeException(u"WorkbenchPanelFactory missing ParentWindow"_ustr, nullptr);
    if (!xFrame.is())
        throw RuntimeException(u"WorkbenchPanelFactory missing Frame"_ustr, nullptr);

    std::unique_ptr<PanelLayout> xPanel;
    if (rResourceURL.endsWith(u"/WorkDashboardPanel"_ustr))
        xPanel = std::make_unique<sfx2::sidebar::WorkDashboardPanel>(pParent);
    else if (rResourceURL.endsWith(u"/LocalNotebookPanel"_ustr))
        xPanel = std::make_unique<sfx2::sidebar::LocalNotebookPanel>(pParent);
    else
        return Reference<ui::XUIElement>();

    return sfx2::sidebar::SidebarPanelBase::Create(rResourceURL, xFrame, std::move(xPanel),
                                                   ui::LayoutSize(0, -1, -1));
}
} // namespace

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface*
org_kqoffice_comp_sfx2_sidebar_WorkbenchPanelFactory_get_implementation(
    css::uno::XComponentContext*, css::uno::Sequence<css::uno::Any> const&)
{
    return cppu::acquire(new WorkbenchPanelFactory);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
