/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W1 Day-1: Evidence).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_EVIDENCERECORDER_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_PROVIDER_EVIDENCERECORDER_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai
{
/// Per-call evidence payload. Caller fills the fields then hands it
/// to EvidenceRecorder::record().
///
/// Schema mirrors docs/schemas/evidence-record.schema.json — the
/// canonical envelope already shipped in V1.5.
struct EvidenceRecord
{
    OUString serviceMode;   // "offline", "private", "cloud"
    OUString provider;      // e.g. "stub", "ollama:qwen2.5:7b"
    OUString capability;    // e.g. "rewrite"
    OUString status;        // "ok", "provider-error", "timeout", ...
    sal_Int32 requestSizeBytes = 0;
    sal_Int32 responseSizeBytes = 0;
    sal_Int32 durationMs = 0;
};

/// Writes JSON evidence records under a per-month directory and mints
/// deterministic, unique `ev-<hex16>` IDs. Failure to write is never
/// fatal — record() returns an empty OUString so the Provider call
/// path can proceed.
///
/// Root directory resolution order:
///   1. env `KQOFFICE_AI_EVIDENCE_DIR` (primarily for cppunit)
///   2. env `TMPDIR` + `/kqoffice-ai-evidence`
///   3. `/tmp/kqoffice-ai-evidence`
class SAL_DLLPUBLIC_EXPORT EvidenceRecorder
{
public:
    /// Assign an id, write `${root}/YYYY-MM/<id>.json`, return id.
    /// Returns empty OUString if writing failed.
    OUString record(const EvidenceRecord& rec);

    /// Exposed for tests — compute the root directory using the same
    /// env precedence as record().
    static OUString resolveRootDir();
};

} // namespace kqoffice::ai

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
