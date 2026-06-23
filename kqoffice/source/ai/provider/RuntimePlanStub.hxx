/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * V2 W4 Day-6 — offline stub that returns v2-w3-runtime-1 JSON for Writer apply.
 */

#pragma once

#include <com/sun/star/ai/XProvider.hpp>
#include <rtl/ustring.hxx>

namespace kqoffice::ai
{
/// Minimal single patch (paragraph-replace) for TryParseApplyPlanRuntimeJson.
OUString buildStubRuntimePlanJson(const css::ai::ProviderRequest& req);
} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */