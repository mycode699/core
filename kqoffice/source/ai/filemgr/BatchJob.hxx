/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (Wave D6: batch convert / archive skeleton).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * DuMate-style bulk format ops — local-only, no cloud.
 * Sequential sync runner, permission-gated. Convert uses UNO Desktop
 * load/export when the process component context is available; set
 * KQOFFICE_BATCH_UNO=0 to force the stub fail path for headless unit tests.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_FILEMGR_BATCHJOB_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_FILEMGR_BATCHJOB_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <atomic>
#include <vector>

#include "PermissionCenter.hxx"

namespace kqoffice::ai::filemgr
{

/// Bulk job kind (convert formats or soft-delete archive).
enum class BatchJobKind : sal_uInt8
{
    ConvertToPdf = 0,
    ConvertToDocx,
    ConvertToXlsx,
    ConvertToPptx,
    SoftDeleteToTrash,
};

/// Job-level lifecycle.
enum class BatchJobState : sal_uInt8
{
    Pending = 0,
    Running,
    Done,
    Failed,
    Cancelled,
};

/// Per-item lifecycle (mirrors job states for progress UI).
enum class BatchItemState : sal_uInt8
{
    Pending = 0,
    Running,
    Done,
    Failed,
    Cancelled,
    Skipped,
};

/// One source path (+ optional explicit target) in a batch job.
struct BatchItem
{
    OUString sourcePath;
    /// Destination path for convert; empty means derive next to source.
    /// Unused for SoftDeleteToTrash (product trash path is chosen by AIFileManager).
    OUString targetPath;
    /// LibreOffice export filter name recorded for convert jobs
    /// (e.g. "writer_pdf_Export", "MS Word 2007 XML").
    OUString exportFilter;
    BatchItemState state = BatchItemState::Pending;
    OUString reasonZh;
    sal_Int64 startedAtMs = 0;
    sal_Int64 finishedAtMs = 0;
};

/// Minimal batch job model (in-memory + optional on-disk ledger).
struct BatchJob
{
    OUString id;
    BatchJobKind kind = BatchJobKind::ConvertToPdf;
    BatchJobState state = BatchJobState::Pending;
    std::vector<BatchItem> items;
    /// Job-level Chinese summary (last run outcome).
    OUString reasonZh;
    sal_Int64 createdAtMs = 0;
    sal_Int64 startedAtMs = 0;
    sal_Int64 finishedAtMs = 0;
    /// Absolute path of the metadata ledger written during run (if any).
    OUString ledgerPath;
    /// Caller-owned cancel flag; also set by requestCancel().
    std::atomic_bool cancelRequested{ false };

    BatchJob() = default;
    BatchJob(const BatchJob&) = delete;
    BatchJob& operator=(const BatchJob&) = delete;
    BatchJob(BatchJob&& other) noexcept;
    BatchJob& operator=(BatchJob&& other) noexcept;
};

/// Aggregate counters after run().
struct BatchRunSummary
{
    sal_Int32 total = 0;
    sal_Int32 done = 0;
    sal_Int32 failed = 0;
    sal_Int32 cancelled = 0;
    sal_Int32 skipped = 0;
    /// True when the job finished every non-cancelled item successfully.
    bool allSucceeded = false;
};

/// Lightweight ledger row for UI (read-only history surface).
struct BatchJobLedgerSummary
{
    OUString id;
    OUString kindZh;
    OUString stateZh;
    OUString reasonZh;
    sal_Int64 finishedAtMs = 0;
    OUString ledgerPath;
};

/// Sequential batch convert / soft-delete runner (sync first iteration).
///
/// Permission rules (must hold for every mutating item):
///   - path must be under PermissionCenter authorized workspace
///   - overwrite / delete require resolveRiskyOp with caller-supplied
///     PermissionDecision (tests pass AllowOnce)
///
/// Convert uses UNO Desktop loadComponentFromURL + XStorable::storeToURL when a
/// process component context is available. Set KQOFFICE_BATCH_UNO=0 to force the
/// stub fail path (unit tests / CI without interactive desktop).
class SAL_DLLPUBLIC_EXPORT BatchJobManager
{
public:
    BatchJobManager();
    ~BatchJobManager();

    /// Create a Pending job with a unique id.
    BatchJob create(BatchJobKind kind) const;

    /// Append a source path. Optional targetPath for convert; ignored for trash.
    /// Returns false if source empty or job is not Pending.
    bool addItem(BatchJob& job, const OUString& sourcePath,
                 const OUString& targetPath = OUString()) const;

    /// Request cooperative cancel; in-flight item finishes, remaining become Cancelled.
    static void requestCancel(BatchJob& job);

    /// Run all items sequentially (sync). Uses PermissionCenter for authorize +
    /// risky-op confirmation. SoftDelete delegates to AIFileManager::moveToTrash.
    /// Convert prefers real UNO export when the office component context is live;
    /// otherwise items fail with a Chinese reasonZh and still write the job ledger.
    BatchRunSummary run(BatchJob& job,
                        kqoffice::ai::control::PermissionDecision confirm) const;

    /// Default LO export filter name for a convert kind (empty for SoftDelete).
    static OUString defaultExportFilter(BatchJobKind kind);

    /// Default target extension including leading dot (e.g. ".pdf").
    static OUString defaultTargetExtension(BatchJobKind kind);

    /// Derive target path: same directory, stem + default extension.
    static OUString deriveTargetPath(const OUString& sourcePath, BatchJobKind kind);

    static OUString kindLabelZh(BatchJobKind kind);
    static OUString jobStateLabelZh(BatchJobState state);
    static OUString itemStateLabelZh(BatchItemState state);

    /// Directory for batch job ledgers ($KQOFFICE_AI_FILEMGR_DIR/batch-jobs or workbench).
    static OUString ledgerRootDir();

    /// Scan ledger dir for recent job summaries (newest first). Read-only; no run.
    static std::vector<BatchJobLedgerSummary> listRecentLedgers(sal_Int32 maxCount = 20);

private:
    void processItem(BatchJob& job, BatchItem& item,
                     kqoffice::ai::control::PermissionDecision confirm) const;
    void processConvert(BatchItem& item, BatchJobKind kind,
                        kqoffice::ai::control::PermissionDecision confirm) const;
    void processSoftDelete(BatchItem& item,
                           kqoffice::ai::control::PermissionDecision confirm) const;
    bool writeLedger(const BatchJob& job) const;
    static bool pathExists(const OUString& systemPath);
    static bool isOfficeProcessAvailable();
};

} // namespace kqoffice::ai::filemgr

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
