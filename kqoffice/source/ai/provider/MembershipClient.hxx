/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 可圈办公 membership client (api.03122.com) — boost / quota helpers.
 * Desktop talks to membership only; LLM upstream stays server-side.
 */
#ifndef INCLUDED_KQOFFICE_AI_PROVIDER_MEMBERSHIPCLIENT_HXX
#define INCLUDED_KQOFFICE_AI_PROVIDER_MEMBERSHIPCLIENT_HXX

#include <sal/config.h>
#include <rtl/ustring.hxx>

namespace kqoffice::ai
{
/// Result of boost/checkin/rush or status probe. Never contains secrets.
struct MembershipBoostResult
{
    bool ok = false;
    OUString messageZh; ///< human summary
    sal_Int32 packs = -1;
    sal_Int32 dayFastRem = -1;
    OUString email;
    OUString rawCode; ///< e.g. invalid_payload / quota_exceeded
};

/// action: "status" | "checkin" | "rush_grab"
/// Uses ~/.config/kqoffice/api-key sessionToken and baseUrl from model-routing
/// (defaults to https://api.03122.com). Never throws.
SAL_DLLPUBLIC_EXPORT MembershipBoostResult membershipBoostAction(const OUString& rAction);

/// One-line status chip (same shape as diagnoseModelRouting membership line).
SAL_DLLPUBLIC_EXPORT OUString membershipQuotaChipZh();

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
