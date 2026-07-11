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
// Offline-mode allow-list. Mirrors W1 spec §"Service Mode Policy",
// extended for five-slot routing (plan/review/agent/extract…).
// Order: most-frequent first, for tiny linear-scan locality.
constexpr std::array<std::u16string_view, 11> kOfflineCapabilities{
    u"rewrite",
    u"summarize",
    u"format-fix",
    u"intent-to-uno",
    u"plan",
    u"review",
    u"agent",
    u"extract",
    u"classify",
    u"verify",
    u"chat",
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

css::uno::Sequence<OUString> ServiceModePolicy::currentAllowlist() const
{
    if (m_mode != Mode::Offline)
        return {};

    css::uno::Sequence<OUString> seq(static_cast<sal_Int32>(kOfflineCapabilities.size()));
    auto* p = seq.getArray();
    sal_Int32 i = 0;
    for (const auto cap : kOfflineCapabilities)
        p[i++] = OUString(cap.data(), cap.size());
    return seq;
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
