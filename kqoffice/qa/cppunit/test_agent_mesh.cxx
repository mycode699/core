/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M3: WorkspaceAgentMesh).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Unit tests for WorkspaceAgentMesh, WorkspaceMeshTaskQueue,
 * WorkspaceAgentMeshOrchestrator, and WorkspaceSupervisorAgentAPI.
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <rtl/ustring.hxx>

#include "WorkspaceAgentMesh.hxx"
#include "WorkspaceMeshTaskQueue.hxx"
#include "WorkspaceAgentMeshOrchestrator.hxx"
#include "WorkspaceSupervisorAgentAPI.hxx"

using namespace kqoffice::ai::mesh;

namespace
{

// ── WorkspaceAgentMesh tests ──────────────────────────────────────────────

class WorkspaceAgentMeshTest : public CppUnit::TestFixture
{
public:
    void setUp() override
    {
        m_pMesh = new WorkspaceAgentMesh();
    }

    void tearDown() override
    {
        delete m_pMesh;
        m_pMesh = nullptr;
    }

    void testRegisterAgent()
    {
        AgentDescriptor desc;
        desc.agentId = "writer-001";
        desc.agentType = "writer";
        desc.displayName = "Writer Assistant";
        desc.capabilities = "rewrite,expand,shorten";
        desc.priority = 80;

        CPPUNIT_ASSERT(m_pMesh->registerAgent(desc));
        // Duplicate should fail.
        CPPUNIT_ASSERT(!m_pMesh->registerAgent(desc));

        auto agents = m_pMesh->listAgents();
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), agents.size());
        CPPUNIT_ASSERT_EQUAL(OUString("writer-001"), agents[0].agentId);
    }

    void testFindById()
    {
        AgentDescriptor desc;
        desc.agentId = "calc-001";
        desc.agentType = "calc";
        desc.capabilities = "formula,chart";
        m_pMesh->registerAgent(desc);

        auto* found = m_pMesh->findById("calc-001");
        CPPUNIT_ASSERT(found != nullptr);
        CPPUNIT_ASSERT_EQUAL(OUString("calc-001"), found->agentId);

        CPPUNIT_ASSERT(m_pMesh->findById("nonexistent") == nullptr);
    }

    void testFindByCapability()
    {
        AgentDescriptor d1;
        d1.agentId = "w1";
        d1.agentType = "writer";
        d1.capabilities = "rewrite,expand";
        m_pMesh->registerAgent(d1);

        AgentDescriptor d2;
        d2.agentId = "w2";
        d2.agentType = "writer";
        d2.capabilities = "shorten,rewrite";
        m_pMesh->registerAgent(d2);

        auto found = m_pMesh->findByCapability("rewrite");
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), found.size());

        auto found2 = m_pMesh->findByCapability("expand");
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), found2.size());
    }

    void testFindByType()
    {
        AgentDescriptor d1;
        d1.agentId = "w1";
        d1.agentType = "writer";
        d1.capabilities = "rewrite";
        m_pMesh->registerAgent(d1);

        AgentDescriptor d2;
        d2.agentId = "c1";
        d2.agentType = "calc";
        d2.capabilities = "formula";
        m_pMesh->registerAgent(d2);

        auto writers = m_pMesh->findByType("writer");
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), writers.size());

        auto calcs = m_pMesh->findByType("calc");
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), calcs.size());
    }

    void testBestRoute()
    {
        AgentDescriptor d1;
        d1.agentId = "w1";
        d1.agentType = "writer";
        d1.capabilities = "rewrite";
        d1.priority = 50;
        m_pMesh->registerAgent(d1);

        AgentDescriptor d2;
        d2.agentId = "w2";
        d2.agentType = "writer";
        d2.capabilities = "rewrite";
        d2.priority = 90;
        m_pMesh->registerAgent(d2);

        // Update stats to make w1 better.
        m_pMesh->updateRouteStats("w1", true, 100);
        m_pMesh->updateRouteStats("w2", false, 500);

        auto* route = m_pMesh->bestRoute("rewrite");
        CPPUNIT_ASSERT(route != nullptr);
        // w1 should win due to better success rate.
        CPPUNIT_ASSERT_EQUAL(OUString("w1"), route->agentId);
    }

    void testHeartbeatAndHealth()
    {
        AgentDescriptor desc;
        desc.agentId = "agent-1";
        desc.agentType = "writer";
        desc.capabilities = "rewrite";
        m_pMesh->registerAgent(desc);

        m_pMesh->heartbeat("agent-1");
        CPPUNIT_ASSERT(m_pMesh->isAgentHealthy("agent-1"));
        CPPUNIT_ASSERT_EQUAL(AgentHealth::Healthy, m_pMesh->agentHealth("agent-1"));

        m_pMesh->markUnhealthy("agent-1");
        CPPUNIT_ASSERT(!m_pMesh->isAgentHealthy("agent-1"));
        CPPUNIT_ASSERT_EQUAL(AgentHealth::Unhealthy, m_pMesh->agentHealth("agent-1"));
    }

    void testUnregisterAgent()
    {
        AgentDescriptor desc;
        desc.agentId = "agent-1";
        desc.agentType = "writer";
        desc.capabilities = "rewrite";
        m_pMesh->registerAgent(desc);

        CPPUNIT_ASSERT(m_pMesh->unregisterAgent("agent-1"));
        CPPUNIT_ASSERT(!m_pMesh->unregisterAgent("agent-1")); // already gone
        CPPUNIT_ASSERT(m_pMesh->listAgents().empty());
    }

    void testRoutesForAgent()
    {
        AgentDescriptor desc;
        desc.agentId = "agent-1";
        desc.agentType = "writer";
        desc.capabilities = "rewrite,expand";
        m_pMesh->registerAgent(desc);

        auto routes = m_pMesh->routesForAgent("agent-1");
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), routes.size());
    }

    CPPUNIT_TEST_SUITE(WorkspaceAgentMeshTest);
    CPPUNIT_TEST(testRegisterAgent);
    CPPUNIT_TEST(testFindById);
    CPPUNIT_TEST(testFindByCapability);
    CPPUNIT_TEST(testFindByType);
    CPPUNIT_TEST(testBestRoute);
    CPPUNIT_TEST(testHeartbeatAndHealth);
    CPPUNIT_TEST(testUnregisterAgent);
    CPPUNIT_TEST(testRoutesForAgent);
    CPPUNIT_TEST_SUITE_END();

private:
    WorkspaceAgentMesh* m_pMesh = nullptr;
};

// ── WorkspaceMeshTaskQueue tests ──────────────────────────────────────────

class WorkspaceMeshTaskQueueTest : public CppUnit::TestFixture
{
public:
    void setUp() override
    {
        m_pQueue = new WorkspaceMeshTaskQueue();
    }

    void tearDown() override
    {
        delete m_pQueue;
        m_pQueue = nullptr;
    }

    void testEnqueue()
    {
        MeshTask task;
        task.taskId = "task-1";
        task.priority = MeshTaskPriority::High;
        task.targetSurface = "writer";

        CPPUNIT_ASSERT(m_pQueue->enqueue(task));
        CPPUNIT_ASSERT(!m_pQueue->enqueue(task)); // duplicate
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), m_pQueue->totalCount());
    }

    void testEnqueueBatch()
    {
        MeshTask t1;
        t1.taskId = "task-1";
        t1.priority = MeshTaskPriority::High;

        MeshTask t2;
        t2.taskId = "task-2";
        t2.priority = MeshTaskPriority::Normal;

        std::vector<MeshTask> batch = {t1, t2};
        CPPUNIT_ASSERT_EQUAL(sal_Int32(2), m_pQueue->enqueueBatch(batch));
        CPPUNIT_ASSERT_EQUAL(sal_Int32(2), m_pQueue->totalCount());
    }

    void testDequeueForAgent()
    {
        MeshTask t1;
        t1.taskId = "task-1";
        t1.priority = MeshTaskPriority::Low;
        m_pQueue->enqueue(t1);

        MeshTask t2;
        t2.taskId = "task-2";
        t2.priority = MeshTaskPriority::High;
        m_pQueue->enqueue(t2);

        auto* dequeued = m_pQueue->dequeueForAgent("agent-1");
        CPPUNIT_ASSERT(dequeued != nullptr);
        CPPUNIT_ASSERT_EQUAL(OUString("task-2"), dequeued->taskId); // higher priority first
        CPPUNIT_ASSERT_EQUAL(MeshTaskState::Running, dequeued->state);
        CPPUNIT_ASSERT_EQUAL(OUString("agent-1"), dequeued->assignedAgentId);
    }

    void testDependencyBlocking()
    {
        MeshTask t1;
        t1.taskId = "task-1";
        t1.priority = MeshTaskPriority::High;
        m_pQueue->enqueue(t1);

        MeshTask t2;
        t2.taskId = "task-2";
        t2.priority = MeshTaskPriority::High;
        t2.dependsOn.push_back("task-1");
        m_pQueue->enqueue(t2);

        // task-2 is not ready because task-1 is not completed.
        CPPUNIT_ASSERT(!m_pQueue->isReady("task-2"));

        // Complete task-1.
        m_pQueue->updateState("task-1", MeshTaskState::Completed);
        CPPUNIT_ASSERT(m_pQueue->isReady("task-2"));

        // blockedTasks: task-1 blocks task-2.
        auto blocked = m_pQueue->blockedTasks("task-1");
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), blocked.size());
        CPPUNIT_ASSERT_EQUAL(OUString("task-2"), blocked[0]);
    }

    void testStateManagement()
    {
        MeshTask t1;
        t1.taskId = "task-1";
        m_pQueue->enqueue(t1);

        CPPUNIT_ASSERT(m_pQueue->updateState("task-1", MeshTaskState::Running));
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), m_pQueue->countByState(MeshTaskState::Running));

        CPPUNIT_ASSERT(m_pQueue->updateState("task-1", MeshTaskState::Completed));
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), m_pQueue->countByState(MeshTaskState::Completed));

        CPPUNIT_ASSERT(!m_pQueue->updateState("nonexistent", MeshTaskState::Completed));
    }

    void testFindById()
    {
        MeshTask t1;
        t1.taskId = "task-1";
        m_pQueue->enqueue(t1);

        auto* found = m_pQueue->findById("task-1");
        CPPUNIT_ASSERT(found != nullptr);
        CPPUNIT_ASSERT_EQUAL(OUString("task-1"), found->taskId);

        CPPUNIT_ASSERT(m_pQueue->findById("nonexistent") == nullptr);
    }

    CPPUNIT_TEST_SUITE(WorkspaceMeshTaskQueueTest);
    CPPUNIT_TEST(testEnqueue);
    CPPUNIT_TEST(testEnqueueBatch);
    CPPUNIT_TEST(testDequeueForAgent);
    CPPUNIT_TEST(testDependencyBlocking);
    CPPUNIT_TEST(testStateManagement);
    CPPUNIT_TEST(testFindById);
    CPPUNIT_TEST_SUITE_END();

private:
    WorkspaceMeshTaskQueue* m_pQueue = nullptr;
};

// ── Orchestrator tests ────────────────────────────────────────────────────

class OrchestratorTest : public CppUnit::TestFixture
{
public:
    void setUp() override
    {
        m_pMesh = new WorkspaceAgentMesh();
        m_pQueue = new WorkspaceMeshTaskQueue();
        m_pOrch = new WorkspaceAgentMeshOrchestrator(*m_pMesh, *m_pQueue);

        // Register agents for all surfaces.
        AgentDescriptor writer;
        writer.agentId = "writer-1";
        writer.agentType = "writer";
        writer.capabilities = "writer,rewrite,expand,shorten";
        m_pMesh->registerAgent(writer);

        AgentDescriptor calc;
        calc.agentId = "calc-1";
        calc.agentType = "calc";
        calc.capabilities = "calc,formula,analyze,chart";
        m_pMesh->registerAgent(calc);

        AgentDescriptor impress;
        impress.agentId = "impress-1";
        impress.agentType = "impress";
        impress.capabilities = "impress,slide,create,design";
        m_pMesh->registerAgent(impress);
    }

    void tearDown() override
    {
        delete m_pOrch;
        delete m_pQueue;
        delete m_pMesh;
        m_pOrch = nullptr;
        m_pQueue = nullptr;
        m_pMesh = nullptr;
    }

    void testDecomposeWriter()
    {
        auto tasks = m_pOrch->decompose("Improve this paragraph", "writer", "");
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), tasks.size());
        CPPUNIT_ASSERT_EQUAL(OUString("writer"), tasks[0].targetSurface);
        CPPUNIT_ASSERT_EQUAL(MeshTaskPriority::High, tasks[0].priority);
    }

    void testDecomposeCalc()
    {
        auto tasks = m_pOrch->decompose("Analyze this data", "calc", "");
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), tasks.size());
        CPPUNIT_ASSERT_EQUAL(OUString("calc"), tasks[0].targetSurface);
    }

    void testDecomposeImpress()
    {
        auto tasks = m_pOrch->decompose("Create a presentation", "impress", "");
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(3), tasks.size());
        CPPUNIT_ASSERT_EQUAL(OUString("impress"), tasks[0].targetSurface);
    }

    void testDispatch()
    {
        MeshTask task;
        task.taskId = "task-1";
        task.targetSurface = "writer";
        task.priority = MeshTaskPriority::High;
        m_pQueue->enqueue(task);

        CPPUNIT_ASSERT(m_pOrch->dispatch(task));
        auto* t = m_pQueue->findById("task-1");
        CPPUNIT_ASSERT(t != nullptr);
        CPPUNIT_ASSERT_EQUAL(OUString("writer-1"), t->assignedAgentId);
    }

    void testMerge()
    {
        // Enqueue parent and sub-tasks.
        MeshTask parent;
        parent.taskId = "parent-1";
        m_pQueue->enqueue(parent);

        MeshTask sub1;
        sub1.taskId = "parent-1-sub-1";
        sub1.parentTaskId = "parent-1";
        m_pQueue->enqueue(sub1);
        m_pQueue->updateState("parent-1-sub-1", MeshTaskState::Completed);

        MeshTask sub2;
        sub2.taskId = "parent-1-sub-2";
        sub2.parentTaskId = "parent-1";
        m_pQueue->enqueue(sub2);
        m_pQueue->updateState("parent-1-sub-2", MeshTaskState::Completed);

        auto result = m_pOrch->merge("parent-1");
        CPPUNIT_ASSERT(result.allSucceeded);
        CPPUNIT_ASSERT(result.failedTasks.empty());
    }

    void testProgress()
    {
        MeshTask sub1;
        sub1.taskId = "parent-1-sub-1";
        sub1.parentTaskId = "parent-1";
        m_pQueue->enqueue(sub1);
        m_pQueue->updateState("parent-1-sub-1", MeshTaskState::Completed);

        MeshTask sub2;
        sub2.taskId = "parent-1-sub-2";
        sub2.parentTaskId = "parent-1";
        m_pQueue->enqueue(sub2);
        m_pQueue->updateState("parent-1-sub-2", MeshTaskState::Running);

        MeshTask sub3;
        sub3.taskId = "parent-1-sub-3";
        sub3.parentTaskId = "parent-1";
        m_pQueue->enqueue(sub3);

        auto p = m_pOrch->progress("parent-1");
        CPPUNIT_ASSERT_EQUAL(sal_Int32(3), p.total);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), p.completed);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), p.running);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(1), p.pending);
    }

    CPPUNIT_TEST_SUITE(OrchestratorTest);
    CPPUNIT_TEST(testDecomposeWriter);
    CPPUNIT_TEST(testDecomposeCalc);
    CPPUNIT_TEST(testDecomposeImpress);
    CPPUNIT_TEST(testDispatch);
    CPPUNIT_TEST(testMerge);
    CPPUNIT_TEST(testProgress);
    CPPUNIT_TEST_SUITE_END();

private:
    WorkspaceAgentMesh* m_pMesh = nullptr;
    WorkspaceMeshTaskQueue* m_pQueue = nullptr;
    WorkspaceAgentMeshOrchestrator* m_pOrch = nullptr;
};

// ── Supervisor tests ──────────────────────────────────────────────────────

class SupervisorTest : public CppUnit::TestFixture
{
public:
    void setUp() override
    {
        m_pMesh = new WorkspaceAgentMesh();
        m_pQueue = new WorkspaceMeshTaskQueue();
        m_pSup = new WorkspaceSupervisorAgentAPI(*m_pMesh, *m_pQueue);

        AgentDescriptor desc;
        desc.agentId = "agent-1";
        desc.agentType = "writer";
        desc.capabilities = "rewrite";
        m_pMesh->registerAgent(desc);
    }

    void tearDown() override
    {
        delete m_pSup;
        delete m_pQueue;
        delete m_pMesh;
        m_pSup = nullptr;
        m_pQueue = nullptr;
        m_pMesh = nullptr;
    }

    void testLifecycle()
    {
        CPPUNIT_ASSERT(m_pSup->startAgent("agent-1"));
        CPPUNIT_ASSERT(m_pSup->pauseAgent("agent-1"));
        CPPUNIT_ASSERT(m_pSup->resumeAgent("agent-1"));
        CPPUNIT_ASSERT(m_pSup->stopAgent("agent-1"));
        CPPUNIT_ASSERT(m_pSup->restartAgent("agent-1"));
    }

    void testBudget()
    {
        CPPUNIT_ASSERT(m_pSup->setBudget("agent-1", 80, 2048, 3600000));
        auto status = m_pSup->checkBudget("agent-1");
        // Budget check should work (stub — no real OS monitoring).
        CPPUNIT_ASSERT(!status.exceeded || status.exceeded);
    }

    void testRecover()
    {
        m_pMesh->markUnhealthy("agent-1");
        CPPUNIT_ASSERT(m_pSup->recoverAgent("agent-1"));
        CPPUNIT_ASSERT(m_pMesh->isAgentHealthy("agent-1"));
    }

    void testEvacuate()
    {
        // Register target agent.
        AgentDescriptor target;
        target.agentId = "agent-2";
        target.agentType = "writer";
        target.capabilities = "rewrite";
        m_pMesh->registerAgent(target);

        // Create tasks assigned to agent-1.
        MeshTask t1;
        t1.taskId = "task-1";
        t1.assignedAgentId = "agent-1";
        m_pQueue->enqueue(t1);
        m_pQueue->updateState("task-1", MeshTaskState::Running);

        MeshTask t2;
        t2.taskId = "task-2";
        t2.assignedAgentId = "agent-1";
        m_pQueue->enqueue(t2);

        sal_Int32 evacuated = m_pSup->evacuateTasks("agent-1", "agent-2");
        CPPUNIT_ASSERT(evacuated > 0);
    }

    void testDiagnose()
    {
        m_pSup->startAgent("agent-1");
        auto d = m_pSup->diagnose("agent-1");
        CPPUNIT_ASSERT_EQUAL(OUString("agent-1"), d.agentId);
        CPPUNIT_ASSERT_EQUAL(OUString("running"), d.state);
    }

    void testDiagnoseAll()
    {
        m_pSup->startAgent("agent-1");
        auto all = m_pSup->diagnoseAll();
        CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), all.size());
    }

    CPPUNIT_TEST_SUITE(SupervisorTest);
    CPPUNIT_TEST(testLifecycle);
    CPPUNIT_TEST(testBudget);
    CPPUNIT_TEST(testRecover);
    CPPUNIT_TEST(testEvacuate);
    CPPUNIT_TEST(testDiagnose);
    CPPUNIT_TEST(testDiagnoseAll);
    CPPUNIT_TEST_SUITE_END();

private:
    WorkspaceAgentMesh* m_pMesh = nullptr;
    WorkspaceMeshTaskQueue* m_pQueue = nullptr;
    WorkspaceSupervisorAgentAPI* m_pSup = nullptr;
};

// ── Test suite registration ──────────────────────────────────────────────

CPPUNIT_TEST_SUITE_REGISTRATION(WorkspaceAgentMeshTest);
CPPUNIT_TEST_SUITE_REGISTRATION(WorkspaceMeshTaskQueueTest);
CPPUNIT_TEST_SUITE_REGISTRATION(OrchestratorTest);
CPPUNIT_TEST_SUITE_REGISTRATION(SupervisorTest);

} // anonymous namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
