/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sfx2/AdditionsDialogHelper.hxx>
#include <officecfg/Office/ExtensionManager.hxx>
#include <sfx2/sfxresid.hxx>
#include <sfx2/strings.hrc>
#include <vcl/svapp.hxx>
#include <vcl/abstdlg.hxx>
#include <vcl/vclenum.hxx>
#include <vcl/weld/MessageDialog.hxx>

namespace
{
void lclShowOnlineResourceUnavailable(weld::Window* pParent)
{
    std::unique_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(
        pParent, VclMessageType::Info, VclButtonsType::Ok,
        SfxResId(STR_ONLINE_RESOURCE_UNAVAILABLE)));
    xBox->run();
}
}

void AdditionsDialogHelper::RunAdditionsDialog(weld::Window* pParent, const OUString& rAdditionsTag)
{
    if (officecfg::Office::ExtensionManager::ExtensionRepositories::CatalogURLBase::get()
            .isEmpty())
    {
        lclShowOnlineResourceUnavailable(pParent);
        return;
    }

    VclAbstractDialogFactory* pFact = VclAbstractDialogFactory::Create();
    VclPtr<AbstractAdditionsDialog> pDialog = pFact->CreateAdditionsDialog(pParent, rAdditionsTag);
    pDialog->StartExecuteAsync(
        [pDialog](sal_Int32 /*nResult*/) -> void { pDialog->disposeOnce(); });
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
