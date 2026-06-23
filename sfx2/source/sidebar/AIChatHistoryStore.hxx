/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 W1: In-app AI chat).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{

class AIChatHistoryStore final
{
public:
    AIChatHistoryStore();

    const OUString& GetDocumentKey() const { return m_sDocumentKey; }
    const OUString& GetStorageRootUrl() const { return m_sStorageRootUrl; }
    const OUString& GetSidecarUrl() const { return m_sSidecarUrl; }

    OUString LoadTranscript() const;
    bool AppendMessage(const OUString& rSpeaker, const OUString& rMessage) const;
    bool Clear() const;

    static OUString ResolveCurrentDocumentIdentity();
    static OUString MakeDocumentHash(const OUString& rDocumentIdentity);

private:
    OUString m_sDocumentKey;
    OUString m_sStorageRootUrl;
    OUString m_sSidecarUrl;
};

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
