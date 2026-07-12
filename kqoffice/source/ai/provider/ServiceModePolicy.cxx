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
#include <cstdlib>
#include <cstring>

namespace kqoffice::ai
{
namespace
{
// Offline/private allow-list. Mirrors W1 spec §"Service Mode Policy",
// extended for five-slot routing and product edit verbs.
constexpr std::array<std::u16string_view, 17> kLocalCapabilities{
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
    u"expand",
    u"translate",
    u"polish",
    u"shorten",
    u"paraphrase",
    u"proofread",
};

bool capabilityAllowed(const OUString& capability)
{
    for (const auto cap : kLocalCapabilities)
    {
        if (capability.equalsIgnoreAsciiCase(OUString(cap.data(),
                                                      static_cast<sal_Int32>(cap.size()))))
            return true;
    }
    return false;
}

bool envTruthy(const char* value)
{
    if (!value || !*value)
        return false;
    if (std::strcmp(value, "1") == 0)
        return true;
    if (std::strcmp(value, "true") == 0 || std::strcmp(value, "TRUE") == 0)
        return true;
    if (std::strcmp(value, "yes") == 0 || std::strcmp(value, "YES") == 0)
        return true;
    return false;
}
} // namespace

ServiceModePolicy::Mode ServiceModePolicy::parseModeName(const OUString& rName)
{
    const OUString n = rName.toAsciiLowerCase().trim();
    if (n == u"private"_ustr)
        return Mode::Private;
    if (n == u"cloud"_ustr)
        return Mode::Cloud;
    return Mode::Offline;
}

ServiceModePolicy::Mode ServiceModePolicy::modeFromEnvironment()
{
    if (const char* raw = std::getenv("KQOFFICE_AI_SERVICE_MODE"))
        return parseModeName(OUString::createFromAscii(raw));
    return Mode::Offline;
}

bool ServiceModePolicy::cloudExplicitlyAllowed()
{
    return envTruthy(std::getenv("KQOFFICE_AI_ALLOW_CLOUD"));
}

ServiceModePolicy::ServiceModePolicy()
    : m_mode(modeFromEnvironment())
    , m_cloudAllowed(cloudExplicitlyAllowed())
{
}

ServiceModePolicy::ServiceModePolicy(Mode eMode)
    : m_mode(eMode)
    , m_cloudAllowed(cloudExplicitlyAllowed())
{
}

bool ServiceModePolicy::allows(const OUString& capability) const
{
    switch (m_mode)
    {
        case Mode::Offline:
        case Mode::Private:
            return capabilityAllowed(capability);
        case Mode::Cloud:
            // Fail-closed: cloud requires explicit opt-in env.
            if (!m_cloudAllowed)
                return false;
            return capabilityAllowed(capability);
    }
    return false;
}

OUString ServiceModePolicy::modeName() const
{
    switch (m_mode)
    {
        case Mode::Offline:
            return u"offline"_ustr;
        case Mode::Private:
            return u"private"_ustr;
        case Mode::Cloud:
            return u"cloud"_ustr;
    }
    return u"offline"_ustr;
}

css::uno::Sequence<OUString> ServiceModePolicy::currentAllowlist() const
{
    // Cloud without explicit opt-in: empty list (fail-closed).
    if (m_mode == Mode::Cloud && !m_cloudAllowed)
        return {};

    // offline / private / (cloud+allow) share the local capability set
    css::uno::Sequence<OUString> seq(static_cast<sal_Int32>(kLocalCapabilities.size()));
    auto* p = seq.getArray();
    sal_Int32 i = 0;
    for (const auto cap : kLocalCapabilities)
        p[i++] = OUString(cap.data(), static_cast<sal_Int32>(cap.size()));
    return seq;
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
