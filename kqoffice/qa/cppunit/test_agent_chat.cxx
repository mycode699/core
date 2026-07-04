/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V4 M2: AgentChat Core).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * V4 M2 Day-1c unit tests for AgentChat classes:
 *   1. AgentChatContextBuilder build() produces correct ChatContext fields.
 *   2. AgentChatContextBuilder toPromptString() formats sections correctly.
 *   3. AgentChatContextBuilder extractUserQuery() strips @mention tokens.
 *   4. ChatContext debugString() produces expected format.
 *   5. AgentChatDiffExtractor extracts operations from ```json blocks.
 *   6. AgentChatDiffExtractor validate() checks required fields.
 *   7. AgentChatDiffExtractor toJson() round-trips operations.
 *   8. AgentChatDiffApplier apply() reports success for known opTypes.
 *   9. AgentChatDiffApplier applyOperation() dispatches by opType.
 *  10. AgentChatDiffApplier canApply() checks operation prerequisites.
 *
 * Pure-logic — no URE / VCL bootstrap (see W1 ProviderTest).
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <rtl/ustring.hxx>

#include <vector>

#include "AgentChatContextBuilder.hxx"
#include "AgentChatDiffExtractor.hxx"
#include "AgentChatDiffApplier.hxx"

using namespace kqoffice::ai::chat;

namespace
{

class AgentChatTest : public CppUnit::TestFixture
{
public:
    void testContextBuilderBuild();
    void testContextBuilderBuildWithSelection();
    void testContextBuilderToPromptString();
    void testContextBuilderExtractUserQuery();
    void testContextBuilderExtractUserQueryMultipleMentions();
    void testChatContextDebugString();
    void testDiffExtractorEmptyOutput();
    void testDiffExtractorExtractOperations();
    void testDiffExtractorExtractNoJsonBlock();
    void testDiffExtractorValidateEmptyPlanId();
    void testDiffExtractorValidateNoOperations();
    void testDiffExtractorValidateMissingFields();
    void testDiffExtractorValidateSuccess();
    void testDiffExtractorToJson();
    void testDiffExtractorToJsonEmpty();
    void testDiffApplierApplySuccess();
    void testDiffApplierApplyOperationInsert();
    void testDiffApplierApplyOperationDelete();
    void testDiffApplierApplyOperationReplace();
    void testDiffApplierApplyOperationFormat();
    void testDiffApplierApplyOperationUnknown();
    void testDiffApplierApplyOperationEmptyOpType();
    void testDiffApplierCanApplyInsert();
    void testDiffApplierCanApplyEmptyFields();
    void testDiffApplierCanApplyUnknownOpType();
    void testDiffApplierCanApplyInsertNoNewText();

    CPPUNIT_TEST_SUITE(AgentChatTest);
    CPPUNIT_TEST(testContextBuilderBuild);
    CPPUNIT_TEST(testContextBuilderBuildWithSelection);
    CPPUNIT_TEST(testContextBuilderToPromptString);
    CPPUNIT_TEST(testContextBuilderExtractUserQuery);
    CPPUNIT_TEST(testContextBuilderExtractUserQueryMultipleMentions);
    CPPUNIT_TEST(testChatContextDebugString);
    CPPUNIT_TEST(testDiffExtractorEmptyOutput);
    CPPUNIT_TEST(testDiffExtractorExtractOperations);
    CPPUNIT_TEST(testDiffExtractorExtractNoJsonBlock);
    CPPUNIT_TEST(testDiffExtractorValidateEmptyPlanId);
    CPPUNIT_TEST(testDiffExtractorValidateNoOperations);
    CPPUNIT_TEST(testDiffExtractorValidateMissingFields);
    CPPUNIT_TEST(testDiffExtractorValidateSuccess);
    CPPUNIT_TEST(testDiffExtractorToJson);
    CPPUNIT_TEST(testDiffExtractorToJsonEmpty);
    CPPUNIT_TEST(testDiffApplierApplySuccess);
    CPPUNIT_TEST(testDiffApplierApplyOperationInsert);
    CPPUNIT_TEST(testDiffApplierApplyOperationDelete);
    CPPUNIT_TEST(testDiffApplierApplyOperationReplace);
    CPPUNIT_TEST(testDiffApplierApplyOperationFormat);
    CPPUNIT_TEST(testDiffApplierApplyOperationUnknown);
    CPPUNIT_TEST(testDiffApplierApplyOperationEmptyOpType);
    CPPUNIT_TEST(testDiffApplierCanApplyInsert);
    CPPUNIT_TEST(testDiffApplierCanApplyEmptyFields);
    CPPUNIT_TEST(testDiffApplierCanApplyUnknownOpType);
    CPPUNIT_TEST(testDiffApplierCanApplyInsertNoNewText);
    CPPUNIT_TEST_SUITE_END();
};

void AgentChatTest::testContextBuilderBuild()
{
    SelectionContext sel;
    sel.surface = u"writer"_ustr;
    sel.text = u""_ustr;

    std::vector<MentionContext> mentions;
    ChatContext ctx = AgentChatContextBuilder::build(u"Hello"_ustr, mentions, sel);

    CPPUNIT_ASSERT_EQUAL(u"writer"_ustr, ctx.documentType);
    CPPUNIT_ASSERT(!ctx.hasSelection());
    CPPUNIT_ASSERT(ctx.systemPrompt.indexOf("document editing") >= 0);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2048), ctx.maxTokens);
}

void AgentChatTest::testContextBuilderBuildWithSelection()
{
    SelectionContext sel;
    sel.surface = u"calc"_ustr;
    sel.text = u"A1:B5=42"_ustr;

    std::vector<MentionContext> mentions;
    ChatContext ctx = AgentChatContextBuilder::build(u"sum these"_ustr, mentions, sel);

    CPPUNIT_ASSERT_EQUAL(u"calc"_ustr, ctx.documentType);
    CPPUNIT_ASSERT(ctx.hasSelection());
    CPPUNIT_ASSERT_EQUAL(u"A1:B5=42"_ustr, ctx.selectionText);
    CPPUNIT_ASSERT(ctx.systemPrompt.indexOf("formula") >= 0);
}

void AgentChatTest::testContextBuilderToPromptString()
{
    ChatContext ctx;
    ctx.documentTitle = u"Report.odt"_ustr;
    ctx.documentType = u"writer"_ustr;
    ctx.selectionText = u"selected paragraph"_ustr;
    ctx.systemPrompt = u"You are a document editing assistant."_ustr;

    OUString prompt = AgentChatContextBuilder::toPromptString(ctx);
    CPPUNIT_ASSERT(prompt.indexOf(u"=== System Instruction ===") >= 0);
    CPPUNIT_ASSERT(prompt.indexOf(u"You are a document editing assistant.") >= 0);
    CPPUNIT_ASSERT(prompt.indexOf(u"--- Document Context ---") >= 0);
    CPPUNIT_ASSERT(prompt.indexOf(u"Report.odt") >= 0);
    CPPUNIT_ASSERT(prompt.indexOf(u"--- Selection ---") >= 0);
    CPPUNIT_ASSERT(prompt.indexOf(u"selected paragraph") >= 0);
    CPPUNIT_ASSERT(prompt.indexOf(u"--- User Request ---") >= 0);
}

void AgentChatTest::testContextBuilderExtractUserQuery()
{
    OUString input = u"Please @selection fix this @doc document"_ustr;

    std::vector<MentionContext> mentions;
    MentionContext m1;
    m1.type = MentionContext::Type::Selection;
    m1.rawText = u"@selection"_ustr;
    mentions.push_back(m1);

    MentionContext m2;
    m2.type = MentionContext::Type::Document;
    m2.rawText = u"@doc"_ustr;
    mentions.push_back(m2);

    OUString query = AgentChatContextBuilder::extractUserQuery(input, mentions);
    CPPUNIT_ASSERT(query.indexOf("@selection") < 0);
    CPPUNIT_ASSERT(query.indexOf("@doc") < 0);
    CPPUNIT_ASSERT(query.indexOf("Please") >= 0);
    CPPUNIT_ASSERT(query.indexOf("fix this") >= 0);
}

void AgentChatTest::testContextBuilderExtractUserQueryMultipleMentions()
{
    OUString input = u"@selection @connector:slack check this"_ustr;

    std::vector<MentionContext> mentions;
    MentionContext m1;
    m1.type = MentionContext::Type::Selection;
    m1.rawText = u"@selection"_ustr;
    mentions.push_back(m1);
    MentionContext m2;
    m2.type = MentionContext::Type::Connector;
    m2.rawText = u"@connector:slack"_ustr;
    mentions.push_back(m2);

    OUString query = AgentChatContextBuilder::extractUserQuery(input, mentions);
    OUString trimmed = query.trim();
    CPPUNIT_ASSERT_EQUAL(u"check this"_ustr, trimmed);
}

void AgentChatTest::testChatContextDebugString()
{
    ChatContext ctx;
    ctx.documentTitle = u"Test.ods"_ustr;
    ctx.documentType = u"calc"_ustr;
    ctx.selectionText = u"data"_ustr;
    ctx.recentMessages.push_back(u"user: hi"_ustr);

    OUString debug = ctx.debugString();
    CPPUNIT_ASSERT(debug.indexOf(u"ChatContext{") >= 0);
    CPPUNIT_ASSERT(debug.indexOf(u"Test.ods") >= 0);
    CPPUNIT_ASSERT(debug.indexOf(u"calc") >= 0);
    CPPUNIT_ASSERT(debug.indexOf(u"hasSelection=true") >= 0);
}

void AgentChatTest::testDiffExtractorEmptyOutput()
{
    ApplyPlan plan = AgentChatDiffExtractor::extract(u""_ustr);
    CPPUNIT_ASSERT(plan.planId.isEmpty());
    CPPUNIT_ASSERT(plan.operations.empty());
}

void AgentChatTest::testDiffExtractorExtractOperations()
{
    OUString output =
        u"Here is the plan:\n"
        u"```json\n"
        u"{\"plan_id\":\"plan.001\",\"operations\":["
        u"{\"op_type\":\"replace\",\"target\":\"para:3\",\"old_text\":\"hello\",\"new_text\":\"hi\"}"
        u"]}\n"
        u"```\n"
        u"Apply this."_ustr;

    ApplyPlan plan = AgentChatDiffExtractor::extract(output);
    CPPUNIT_ASSERT_EQUAL(u"plan.001"_ustr, plan.planId);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), plan.operations.size());
    CPPUNIT_ASSERT_EQUAL(u"replace"_ustr, plan.operations[0].opType);
    CPPUNIT_ASSERT_EQUAL(u"para:3"_ustr, plan.operations[0].target);
    CPPUNIT_ASSERT_EQUAL(u"hello"_ustr, plan.operations[0].oldText);
    CPPUNIT_ASSERT_EQUAL(u"hi"_ustr, plan.operations[0].newText);
}

void AgentChatTest::testDiffExtractorExtractNoJsonBlock()
{
    OUString output = u"Just plain text, no JSON block."_ustr;
    ApplyPlan plan = AgentChatDiffExtractor::extract(output);
    CPPUNIT_ASSERT(plan.planId.isEmpty());
    CPPUNIT_ASSERT(plan.operations.empty());
}

void AgentChatTest::testDiffExtractorValidateEmptyPlanId()
{
    ApplyPlan plan;
    plan.planId = u""_ustr;
    plan.operations.push_back({u"replace"_ustr, u"para:1"_ustr, u""_ustr, u"x"_ustr});
    CPPUNIT_ASSERT(!AgentChatDiffExtractor::validate(plan));
}

void AgentChatTest::testDiffExtractorValidateNoOperations()
{
    ApplyPlan plan;
    plan.planId = u"plan.001"_ustr;
    CPPUNIT_ASSERT(!AgentChatDiffExtractor::validate(plan));
}

void AgentChatTest::testDiffExtractorValidateMissingFields()
{
    ApplyPlan plan;
    plan.planId = u"plan.001"_ustr;
    plan.operations.push_back({u""_ustr, u""_ustr, u""_ustr, u""_ustr});
    CPPUNIT_ASSERT(!AgentChatDiffExtractor::validate(plan));
}

void AgentChatTest::testDiffExtractorValidateSuccess()
{
    ApplyPlan plan;
    plan.planId = u"plan.001"_ustr;
    plan.operations.push_back({u"replace"_ustr, u"para:1"_ustr, u"old"_ustr, u"new"_ustr});
    CPPUNIT_ASSERT(AgentChatDiffExtractor::validate(plan));
}

void AgentChatTest::testDiffExtractorToJson()
{
    ApplyPlan plan;
    plan.planId = u"plan.001"_ustr;
    plan.operations.push_back({u"replace"_ustr, u"para:3"_ustr, u"hello"_ustr, u"hi"_ustr});
    plan.operations.push_back({u"insert"_ustr, u"end"_ustr, u""_ustr, u"appendix"_ustr});

    OUString json = AgentChatDiffExtractor::toJson(plan);
    CPPUNIT_ASSERT(json.indexOf(u"plan.001") >= 0);
    CPPUNIT_ASSERT(json.indexOf(u"replace") >= 0);
    CPPUNIT_ASSERT(json.indexOf(u"para:3") >= 0);
    CPPUNIT_ASSERT(json.indexOf(u"insert") >= 0);
    CPPUNIT_ASSERT(json.indexOf(u"appendix") >= 0);
}

void AgentChatTest::testDiffExtractorToJsonEmpty()
{
    ApplyPlan plan;
    OUString json = AgentChatDiffExtractor::toJson(plan);
    CPPUNIT_ASSERT(json.indexOf(u"plan_id") >= 0);
    CPPUNIT_ASSERT(json.indexOf(u"operations") >= 0);
}

void AgentChatTest::testDiffApplierApplySuccess()
{
    ApplyPlan plan;
    plan.planId = u"plan.001"_ustr;
    plan.operations.push_back({u"replace"_ustr, u"para:1"_ustr, u"old"_ustr, u"new"_ustr});
    plan.operations.push_back({u"insert"_ustr, u"para:2"_ustr, u""_ustr, u"extra"_ustr});

    ApplyResult result = AgentChatDiffApplier::apply(plan);
    CPPUNIT_ASSERT(result.success);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), result.appliedOps.size());
    CPPUNIT_ASSERT(result.error.isEmpty());
}

void AgentChatTest::testDiffApplierApplyOperationInsert()
{
    DiffOperation op;
    op.opType = u"insert"_ustr;
    op.target = u"para:5"_ustr;
    op.newText = u"new paragraph"_ustr;

    ApplyResult result = AgentChatDiffApplier::applyOperation(op);
    CPPUNIT_ASSERT(result.success);
    CPPUNIT_ASSERT(result.error.isEmpty());
}

void AgentChatTest::testDiffApplierApplyOperationDelete()
{
    DiffOperation op;
    op.opType = u"delete"_ustr;
    op.target = u"para:3"_ustr;
    op.oldText = u"remove this"_ustr;

    ApplyResult result = AgentChatDiffApplier::applyOperation(op);
    CPPUNIT_ASSERT(result.success);
    CPPUNIT_ASSERT(result.error.isEmpty());
}

void AgentChatTest::testDiffApplierApplyOperationReplace()
{
    DiffOperation op;
    op.opType = u"replace"_ustr;
    op.target = u"cell:A1"_ustr;
    op.oldText = u"42"_ustr;
    op.newText = u"100"_ustr;

    ApplyResult result = AgentChatDiffApplier::applyOperation(op);
    CPPUNIT_ASSERT(result.success);
    CPPUNIT_ASSERT(result.error.isEmpty());
}

void AgentChatTest::testDiffApplierApplyOperationFormat()
{
    DiffOperation op;
    op.opType = u"format"_ustr;
    op.target = u"slide:3"_ustr;

    ApplyResult result = AgentChatDiffApplier::applyOperation(op);
    CPPUNIT_ASSERT(result.success);
    CPPUNIT_ASSERT(result.error.isEmpty());
}

void AgentChatTest::testDiffApplierApplyOperationUnknown()
{
    DiffOperation op;
    op.opType = u"delete-system"_ustr;
    op.target = u"para:1"_ustr;

    ApplyResult result = AgentChatDiffApplier::applyOperation(op);
    CPPUNIT_ASSERT(!result.success);
    CPPUNIT_ASSERT(!result.error.isEmpty());
    CPPUNIT_ASSERT(result.error.indexOf("Unknown") >= 0);
}

void AgentChatTest::testDiffApplierApplyOperationEmptyOpType()
{
    DiffOperation op;
    op.opType = u""_ustr;
    op.target = u"para:1"_ustr;

    ApplyResult result = AgentChatDiffApplier::applyOperation(op);
    CPPUNIT_ASSERT(!result.success);
    CPPUNIT_ASSERT(result.error.indexOf("Empty") >= 0);
}

void AgentChatTest::testDiffApplierCanApplyInsert()
{
    DiffOperation op;
    op.opType = u"insert"_ustr;
    op.target = u"para:2"_ustr;
    op.newText = u"content"_ustr;
    CPPUNIT_ASSERT(AgentChatDiffApplier::canApply(op));
}

void AgentChatTest::testDiffApplierCanApplyEmptyFields()
{
    DiffOperation op;
    CPPUNIT_ASSERT(!AgentChatDiffApplier::canApply(op));

    op.opType = u"delete"_ustr;
    CPPUNIT_ASSERT(!AgentChatDiffApplier::canApply(op));
}

void AgentChatTest::testDiffApplierCanApplyUnknownOpType()
{
    DiffOperation op;
    op.opType = u"reboot"_ustr;
    op.target = u"system"_ustr;
    CPPUNIT_ASSERT(!AgentChatDiffApplier::canApply(op));
}

void AgentChatTest::testDiffApplierCanApplyInsertNoNewText()
{
    DiffOperation op;
    op.opType = u"insert"_ustr;
    op.target = u"end"_ustr;
    // newText intentionally empty
    CPPUNIT_ASSERT(!AgentChatDiffApplier::canApply(op));
}

CPPUNIT_TEST_SUITE_REGISTRATION(AgentChatTest);
} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */