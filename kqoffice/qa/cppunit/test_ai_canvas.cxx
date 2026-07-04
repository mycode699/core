/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V5: AI Canvas Mode).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Unit tests for AICanvasMode, AICanvasUI, and AICanvasIntegration.
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <rtl/ustring.hxx>

#include "AICanvasMode.hxx"
#include "AICanvasUI.hxx"
#include "AICanvasIntegration.hxx"

using namespace kqoffice::ai::canvas;

namespace
{

// ── AICanvasMode tests ──────────────────────────────────────────────────

class AICanvasModeTest : public CppUnit::TestFixture
{
public:
    void setUp() override
    {
        m_pCanvas = new AICanvasMode();
    }

    void tearDown() override
    {
        delete m_pCanvas;
        m_pCanvas = nullptr;
    }

    void testStartSession()
    {
        auto session = m_pCanvas->startSession(CanvasDocType::Writer,
            u"创建一个项目计划文档"_ustr);
        CPPUNIT_ASSERT(m_pCanvas->isActive());
        CPPUNIT_ASSERT_EQUAL(CanvasState::Describing, session.state);
        CPPUNIT_ASSERT_EQUAL(CanvasDocType::Writer, session.docType);
        CPPUNIT_ASSERT(session.estimatedSteps >= 3);
        CPPUNIT_ASSERT(session.estimatedSteps <= 10);
        CPPUNIT_ASSERT(!session.sessionId.isEmpty());
    }

    void testCancelSession()
    {
        m_pCanvas->startSession(CanvasDocType::Calc,
            u"创建财务报表"_ustr);
        CPPUNIT_ASSERT(m_pCanvas->isActive());
        m_pCanvas->cancelSession();
        CPPUNIT_ASSERT(!m_pCanvas->isActive());
        CPPUNIT_ASSERT_EQUAL(CanvasState::Cancelled, m_pCanvas->getSession().state);
    }

    void testProgress()
    {
        m_pCanvas->startSession(CanvasDocType::Writer,
            u"短目标"_ustr);
        // Initial progress should be 0
        CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, m_pCanvas->progress(), 0.01);

        // After submit + confirm one step
        m_pCanvas->submitRequirement(u"创建标题和概述"_ustr);
        m_pCanvas->confirmStep();
        CPPUNIT_ASSERT(m_pCanvas->progress() > 0.0);
    }

    void testEstimateSteps()
    {
        // Short goal → 3 steps
        auto s1 = m_pCanvas->startSession(CanvasDocType::Writer,
            u"简历"_ustr);
        CPPUNIT_ASSERT_EQUAL(sal_Int32(3), s1.estimatedSteps);

        // Medium goal → 5 steps
        auto s2 = m_pCanvas->startSession(CanvasDocType::Writer,
            u"创建一个详细的项目计划文档包含时间线"_ustr);
        CPPUNIT_ASSERT(s2.estimatedSteps >= 4);
    }

    void testStatusString()
    {
        m_pCanvas->startSession(CanvasDocType::Writer, u"测试"_ustr);
        OUString status = m_pCanvas->statusString();
        CPPUNIT_ASSERT(!status.isEmpty());
        CPPUNIT_ASSERT(status.indexOf("0/") >= 0);
    }

    void testStateLabel()
    {
        CPPUNIT_ASSERT(!AICanvasMode::stateLabel(CanvasState::Idle).isEmpty());
        CPPUNIT_ASSERT(!AICanvasMode::stateLabel(CanvasState::Describing).isEmpty());
        CPPUNIT_ASSERT(!AICanvasMode::stateLabel(CanvasState::Completed).isEmpty());
        CPPUNIT_ASSERT(!AICanvasMode::stateLabel(CanvasState::Cancelled).isEmpty());
    }

    CPPUNIT_TEST_SUITE(AICanvasModeTest);
    CPPUNIT_TEST(testStartSession);
    CPPUNIT_TEST(testCancelSession);
    CPPUNIT_TEST(testProgress);
    CPPUNIT_TEST(testEstimateSteps);
    CPPUNIT_TEST(testStatusString);
    CPPUNIT_TEST(testStateLabel);
    CPPUNIT_TEST_SUITE_END();

private:
    AICanvasMode* m_pCanvas = nullptr;
};

// ── AICanvasUI tests ────────────────────────────────────────────────────

class AICanvasUITest : public CppUnit::TestFixture
{
public:
    void testFormatProgressBar()
    {
        OUString bar = AICanvasUI::formatProgressBar(0.5, 10);
        CPPUNIT_ASSERT(bar.indexOf("50%") >= 0);
        CPPUNIT_ASSERT(bar.indexOf("█") >= 0);

        OUString bar0 = AICanvasUI::formatProgressBar(0.0, 10);
        CPPUNIT_ASSERT(bar0.indexOf("0%") >= 0);

        OUString bar100 = AICanvasUI::formatProgressBar(1.0, 10);
        CPPUNIT_ASSERT(bar100.indexOf("100%") >= 0);
    }

    void testBuildDisplay()
    {
        AICanvasMode canvas;
        canvas.startSession(CanvasDocType::Writer, u"测试文档"_ustr);

        auto display = AICanvasUI::buildDisplay(canvas.getSession());
        CPPUNIT_ASSERT(!display.stepLabel.isEmpty());
        CPPUNIT_ASSERT(!display.progressBar.isEmpty());
        CPPUNIT_ASSERT(!display.statusText.isEmpty());
        CPPUNIT_ASSERT(!display.nextActionHint.isEmpty());
    }

    void testFormatStepSummary()
    {
        CanvasStep step;
        step.stepNumber = 3;
        step.userRequirement = u"添加结论部分"_ustr;
        step.confirmed = true;

        OUString summary = AICanvasUI::formatStepSummary(step);
        CPPUNIT_ASSERT(summary.indexOf("Step 3") >= 0);
        CPPUNIT_ASSERT(summary.indexOf("✓") >= 0);
        CPPUNIT_ASSERT(summary.indexOf("添加结论部分") >= 0);
    }

    CPPUNIT_TEST_SUITE(AICanvasUITest);
    CPPUNIT_TEST(testFormatProgressBar);
    CPPUNIT_TEST(testBuildDisplay);
    CPPUNIT_TEST(testFormatStepSummary);
    CPPUNIT_TEST_SUITE_END();
};

// ── AICanvasIntegration tests ───────────────────────────────────────────

class AICanvasIntegrationTest : public CppUnit::TestFixture
{
public:
    void tearDown() override
    {
        if (AICanvasIntegration::isCanvasActive())
            AICanvasIntegration::endCanvasSession();
    }

    void testStartEndSession()
    {
        CPPUNIT_ASSERT(!AICanvasIntegration::isCanvasActive());

        bool started = AICanvasIntegration::startCanvasViaChat(
            CanvasDocType::Writer, u"测试集成"_ustr);
        CPPUNIT_ASSERT(started);
        CPPUNIT_ASSERT(AICanvasIntegration::isCanvasActive());

        auto& session = AICanvasIntegration::getCurrentSession();
        CPPUNIT_ASSERT_EQUAL(CanvasDocType::Writer, session.docType);
        CPPUNIT_ASSERT_EQUAL(OUString("测试集成"), session.overallGoal);

        AICanvasIntegration::endCanvasSession();
        CPPUNIT_ASSERT(!AICanvasIntegration::isCanvasActive());
    }

    void testProcessMessage()
    {
        AICanvasIntegration::startCanvasViaChat(CanvasDocType::Writer,
            u"创建标题页"_ustr);

        OUString response = AICanvasIntegration::processCanvasMessage(
            u"创建一个文档标题"_ustr);
        CPPUNIT_ASSERT(!response.isEmpty());
    }

    CPPUNIT_TEST_SUITE(AICanvasIntegrationTest);
    CPPUNIT_TEST(testStartEndSession);
    CPPUNIT_TEST(testProcessMessage);
    CPPUNIT_TEST_SUITE_END();
};

// ── Test suite registration ──────────────────────────────────────────────

CPPUNIT_TEST_SUITE_REGISTRATION(AICanvasModeTest);
CPPUNIT_TEST_SUITE_REGISTRATION(AICanvasUITest);
CPPUNIT_TEST_SUITE_REGISTRATION(AICanvasIntegrationTest);

} // anonymous namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
