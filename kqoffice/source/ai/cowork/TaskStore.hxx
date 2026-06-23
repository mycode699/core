/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W5 Day-0: Async Cowork Task Store).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKSTORE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKSTORE_HXX

#include "AsyncTask.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::cowork
{
/// JSON-on-disk store for AsyncTaskEnvelope records. Layout mirrors
/// EvidenceRecorder: `${root}/YYYY-MM/<task_id>.json`.
///
/// Root directory resolution order:
///   1. env `KQOFFICE_AI_TASKS_DIR` (primarily for cppunit / tests)
///   2. env `TMPDIR` + `/kqoffice-ai-tasks`
///   3. `/tmp/kqoffice-ai-tasks`
///
/// Day-0 ships write + read + list-by-state. No migration logic, no
/// schema-version negotiation. All on-disk envelopes today are
/// `schema_version=1`.
class SAL_DLLPUBLIC_EXPORT TaskStore
{
public:
    /// Write `${root}/YYYY-MM/<task_id>.json`. Returns true on success.
    /// Uses envelope.createdAt's first 7 chars (YYYY-MM) for the
    /// month directory; falls back to current UTC if createdAt empty.
    bool write(const AsyncTaskEnvelope& env);

    /// Read `${root}/YYYY-MM/<task_id>.json`. Caller supplies the
    /// month dir token (YYYY-MM) since task_id alone does not encode
    /// month. Returns true on success and populates `out`.
    bool read(const OUString& monthDir,
              const OUString& taskId,
              AsyncTaskEnvelope& out);

    /// List task ids (without `.json`) for the given month directory
    /// whose envelope state matches `state`. Empty vector on missing
    /// directory or read errors.
    std::vector<OUString> listByState(const OUString& monthDir,
                                      TaskState state);

    /// Exposed for tests — compute the root directory using the env
    /// precedence above.
    static OUString resolveRootDir();
};

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
