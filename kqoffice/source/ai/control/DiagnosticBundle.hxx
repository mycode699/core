/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (AI workbench: diagnostic export).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Assembles a redacted on-disk diagnostic pack (directory of text/json).
 * No API keys, tokens, or document body content.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_DIAGNOSTICBUNDLE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CONTROL_DIAGNOSTICBUNDLE_HXX

#include "ErrorClassifier.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::control
{

struct DiagnosticErrorSample
{
    OUString message;
    OUString errorCodeHint;
    sal_Int64 atMs = 0;
};

struct DiagnosticBundleRequest
{
    /// Output directory (created if missing). Empty → temp under kqofficeTempDir.
    OUString outputDir;
    /// Optional product version string (caller supplies; no auto-detect required).
    OUString productVersion;
    OUString buildId;
    OUString platformHint; ///< e.g. macos-arm64, windows-x64
    /// Recent errors (ring); will be classified + redacted.
    std::vector<DiagnosticErrorSample> recentErrors;
    /// Extra free-form lines already redacted by caller (optional).
    std::vector<OUString> notes;
    bool includePermissionSummary = true;
    bool includeResourceEnvelope = true;
    bool includeSafeRestoreDoctor = true;
};

struct DiagnosticBundleResult
{
    bool success = false;
    OUString outputDir;
    OUString manifestPath;
    OUString summaryZh;
    OUString error;
    sal_Int32 fileCount = 0;
};

/// Build a support/diagnostic pack. Pure filesystem + existing control plane.
class SAL_DLLPUBLIC_EXPORT DiagnosticBundle
{
public:
    static DiagnosticBundleResult exportBundle(const DiagnosticBundleRequest& req);

    /// Default dir: {temp}/kqoffice-diag-{timestamp}
    static OUString defaultOutputDir();

    /// Redact + classify one sample to a single JSON object line (no secrets).
    static OUString errorSampleJson(const DiagnosticErrorSample& sample);

    /// Manifest schema version for consumers.
    static OUString schemaVersion();
};

} // namespace kqoffice::ai::control

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
