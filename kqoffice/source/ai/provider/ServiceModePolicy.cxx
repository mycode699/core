/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W1: Provider Runtime).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "ServiceModePolicy.hxx"

#include <array>

namespace kqoffice::ai
{
namespace
{
// Offline-mode allow-list. Mirrors W1 spec §"Service Mode Policy".
// Order: most-frequent first, for tiny linear-scan locality.
constexpr std::array<std::u16string_view, 4> kOfflineCapabilities{
    u"rewrite",
    u"summarize",
    u"format-fix",
    u"intent-to-uno",
};
} // namespace

ServiceModePolicy::ServiceModePolicy()
    : m_mode(Mode::Offline)
{
}

bool ServiceModePolicy::allows(const OUString& capability) const
{
    if (m_mode != Mode::Offline)
        return false;

    for (const auto cap : kOfflineCapabilities)
    {
        if (capability.equalsIgnoreAsciiCase(OUString(cap.data(), cap.size())))
            return true;
    }
    return false;
}

OUString ServiceModePolicy::modeName() const
{
    switch (m_mode)
    {
        case Mode::Offline: return u"offline"_ustr;
        case Mode::Private: return u"private"_ustr;
        case Mode::Cloud:   return u"cloud"_ustr;
    }
    return u"offline"_ustr;
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
