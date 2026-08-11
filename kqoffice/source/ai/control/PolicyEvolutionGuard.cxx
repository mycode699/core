/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI harness: evolution guard).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "PolicyEvolutionGuard.hxx"

namespace kqoffice::ai::control
{

std::vector<OUString> PolicyEvolutionGuard::allowedDialFields()
{
    return {
        u"router_direct_max_chars"_ustr,
        u"router_plan_min_chars"_ustr,
        u"router_confidence_floor"_ustr,
        u"detector_stall_seconds"_ustr,
        u"composer_queue_max"_ustr,
        u"resource_membership_poll_ms"_ustr,
        u"resource_warmup_ms"_ustr,
        u"resource_fts_top_k"_ustr,
    };
}

std::vector<OUString> PolicyEvolutionGuard::forbiddenMarkers()
{
    // Intentionally broad: sandbox, writeback defaults, credentials, promotion.
    return {
        u"sandbox"_ustr,      u"capab"_ustr,       u"external_write"_ustr, u"externalwrite"_ustr,
        u"retention"_ustr,    u"evaluator"_ustr,   u"approval"_ustr,       u"approve"_ustr,
        u"promot"_ustr,       u"yolo"_ustr,        u"writeback"_ustr,      u"credential"_ustr,
        u"keychain"_ustr,     u"token"_ustr,       u"egress"_ustr,         u"full_disk"_ustr,
        u"fulldisk"_ustr,     u"grant_all"_ustr,   u"always_allow"_ustr,   u"human_approval"_ustr,
    };
}

PolicyCandidateCheck PolicyEvolutionGuard::validateFieldName(const OUString& fieldName)
{
    PolicyCandidateCheck c;
    const OUString low = fieldName.toAsciiLowerCase().trim();
    if (low.isEmpty())
    {
        c.allowed = false;
        c.reasonCode = u"empty-field"_ustr;
        c.reasonZh = u"空字段名"_ustr;
        return c;
    }

    // Exact allow-list first (may contain substrings that look forbidden).
    for (const auto& a : allowedDialFields())
    {
        if (low == a.toAsciiLowerCase())
        {
            c.allowed = true;
            c.reasonCode = u"allowed-dial"_ustr;
            c.reasonZh = u"允许调参字段"_ustr;
            return c;
        }
    }

    for (const auto& m : forbiddenMarkers())
    {
        if (low.indexOf(m) >= 0)
        {
            c.allowed = false;
            c.rejectedField = fieldName;
            c.reasonCode = u"forbidden-marker"_ustr;
            c.reasonZh = u"禁止触碰安全边界字段 · "_ustr + m;
            return c;
        }
    }

    c.allowed = false;
    c.rejectedField = fieldName;
    c.reasonCode = u"not-on-allow-list"_ustr;
    c.reasonZh = u"未在允许调参列表中 · 需人工加白名单"_ustr;
    return c;
}

PolicyCandidateCheck PolicyEvolutionGuard::validateCandidateFields(const std::vector<OUString>& fields)
{
    if (fields.empty())
    {
        PolicyCandidateCheck c;
        c.allowed = false;
        c.reasonCode = u"empty-candidate"_ustr;
        c.reasonZh = u"空候选"_ustr;
        return c;
    }
    for (const auto& f : fields)
    {
        auto one = validateFieldName(f);
        if (!one.allowed)
            return one;
    }
    PolicyCandidateCheck ok;
    ok.allowed = true;
    ok.reasonCode = u"candidate-ok"_ustr;
    ok.reasonZh = u"候选字段均在允许调参列表（仍须人工晋升）"_ustr;
    return ok;
}

bool PolicyEvolutionGuard::ceilingMayOnlyFall(sal_Int32 oldValue, sal_Int32 newValue)
{
    return newValue <= oldValue;
}

OUString PolicyEvolutionGuard::policyHintZh()
{
    return u"策略候选仅可改路由/检测/队列等调参；不可改写回默认、沙箱、密钥、YOLO。"
           "晋升必须由人确认。"_ustr;
}

} // namespace kqoffice::ai::control

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
