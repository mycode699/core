/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V2 W5: Async Cowork Task Queue).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKQUEUE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_COWORK_TASKQUEUE_HXX

#include "AsyncTask.hxx"
#include "TaskStore.hxx"

#include <rtl/ustring.hxx>
#include <sal/types.h>

namespace kqoffice::ai::cowork
{
/// Pure-logic queue coordinator for W5 offline mode. It persists every
/// transition through TaskStore and intentionally does not own worker threads
/// or provider calls; those layers can build on this state lifecycle.
class SAL_DLLPUBLIC_EXPORT TaskQueue
{
public:
    explicit TaskQueue(TaskStore& store, sal_Int32 maxParallelism = 1);

    bool enqueue(AsyncTaskEnvelope env);
    bool dispatchNext(const OUString& monthDir, AsyncTaskEnvelope& out);

    bool markAwaitingReview(const OUString& monthDir,
                            const OUString& taskId,
                            const OUString& resultPlanId,
                            const OUString& evidenceId,
                            AsyncTaskEnvelope* out = nullptr);
    bool markApplied(const OUString& monthDir,
                     const OUString& taskId,
                     AsyncTaskEnvelope* out = nullptr);
    bool cancel(const OUString& monthDir,
                const OUString& taskId,
                AsyncTaskEnvelope* out = nullptr);
    bool cancel(const OUString& monthDir,
                const OUString& taskId,
                const OUString& reason,
                AsyncTaskEnvelope* out = nullptr);
    bool markFailed(const OUString& monthDir,
                    const OUString& taskId,
                    const OUString& reason,
                    AsyncTaskEnvelope* out = nullptr);
    bool refineFailed(const OUString& monthDir,
                      const OUString& failedTaskId,
                      const OUString& newTaskId,
                      const OUString& refinedPrompt,
                      AsyncTaskEnvelope& out);

    sal_Int32 runningCount(const OUString& monthDir);

private:
    bool transition(const OUString& monthDir,
                    const OUString& taskId,
                    TaskState to,
                    const OUString& reason,
                    const OUString& resultPlanId,
                    AsyncTaskEnvelope* out);

    TaskStore& m_store;
    sal_Int32 m_maxParallelism;
};

} // namespace kqoffice::ai::cowork

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
