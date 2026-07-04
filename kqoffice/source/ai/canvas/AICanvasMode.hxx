/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V5: AI Canvas Mode).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * AI Canvas Mode — state machine and core workflow types.
 *
 * Guided document creation: user describes what they want in natural
 * language, LLM generates content step by step, user confirms/revises
 * each step. Typically 3-10 steps to complete a full document.
 */

#ifndef INCLUDED_KQOFFICE_SOURCE_AI_CANVAS_AICANVASMODE_HXX
#define INCLUDED_KQOFFICE_SOURCE_AI_CANVAS_AICANVASMODE_HXX

#include <rtl/ustring.hxx>
#include <sal/types.h>

#include <vector>

namespace kqoffice::ai::canvas
{

/// Document types supported by canvas mode.
enum class CanvasDocType : sal_uInt8
{
    Writer = 0,
    Calc = 1,
    Impress = 2,
};

/// Canvas workflow states.
enum class CanvasState : sal_uInt8
{
    Idle = 0,        // Not started
    Describing,      // User is describing requirements
    Generating,      // LLM is generating content
    Reviewing,       // User is reviewing generated content
    Revising,        // User requested changes, LLM revising
    Confirmed,       // Step confirmed, moving to next
    Completed,       // All steps done
    Cancelled,       // User cancelled
};

/// A single step in the canvas workflow.
struct CanvasStep
{
    sal_Int32 stepNumber = 0;       // 1-based step number
    OUString description;           // What this step aims to accomplish
    OUString userRequirement;       // User's natural language input
    OUString generatedContent;      // LLM-generated content for this step
    OUString diffPlanId;            // ApplyPlan ID for this step's changes
    CanvasState state = CanvasState::Idle;
    bool confirmed = false;
};

/// The full canvas session.
struct CanvasSession
{
    OUString sessionId;
    CanvasDocType docType = CanvasDocType::Writer;
    OUString overallGoal;           // User's high-level goal
    std::vector<CanvasStep> steps;  // All steps in this session
    CanvasState state = CanvasState::Idle;
    sal_Int32 currentStep = 0;      // 0-based index into steps
    sal_Int32 estimatedSteps = 5;   // Estimated total steps (3-10)
    OUString documentTitle;         // Generated or user-provided title
};

/// Result of submitting a requirement for the current step.
struct CanvasSubmitResult
{
    bool success = false;
    OUString error;
    CanvasStep step;
    OUString generatedContent;
};

/// Result of confirming/rejecting a step.
struct CanvasConfirmResult
{
    bool success = false;
    OUString error;
    bool isComplete = false;        // True if all estimated steps are done
    CanvasStep nextStep;            // The next step template (if not complete)
};

class SAL_DLLPUBLIC_EXPORT AICanvasMode
{
public:
    AICanvasMode();
    ~AICanvasMode();

    // ── Session management ──────────────────────────────────────────────

    /// Start a new canvas session for the given document type and goal.
    CanvasSession startSession(CanvasDocType docType,
                               const OUString& overallGoal);

    /// Get the current session (const access).
    const CanvasSession& getSession() const { return m_session; }

    /// Cancel the current session.
    void cancelSession();

    /// Check if a session is active.
    bool isActive() const { return m_session.state != CanvasState::Idle
                                && m_session.state != CanvasState::Cancelled; }

    // ── Step workflow ───────────────────────────────────────────────────

    /// Submit a natural language requirement for the current step.
    /// LLM generates content based on the requirement and prior context.
    CanvasSubmitResult submitRequirement(const OUString& requirement);

    /// Confirm the current step's generated content.
    /// Advances to the next step if more are needed.
    CanvasConfirmResult confirmStep();

    /// Reject the current step and provide revision feedback.
    /// LLM regenerates content based on the feedback.
    CanvasSubmitResult reviseStep(const OUString& feedback);

    /// Skip the current step without confirmation.
    CanvasConfirmResult skipStep();

    // ── Content application ─────────────────────────────────────────────

    /// Apply all confirmed steps to the document via DiffApplier.
    bool applyAllSteps();

    /// Apply a single confirmed step by index.
    bool applyStep(sal_Int32 stepIndex);

    // ── Progress ────────────────────────────────────────────────────────

    /// Get the current progress as a fraction (0.0 - 1.0).
    double progress() const;

    /// Get a user-friendly status string.
    OUString statusString() const;

    /// Get the current state label for UI display.
    static OUString stateLabel(CanvasState state);

    // ── Undo ─────────────────────────────────────────────────────────────

    /// Undo the last applied step via the DiffApplier undo stack.
    bool undoLastStep();

    /// Check whether undo is available.
    bool canUndo() const;

private:
    /// Generate content via LLM for a given requirement and context.
    OUString generateContent(const OUString& requirement,
                             const OUString& context);

    /// Build context string from all prior confirmed steps.
    OUString buildContext() const;

    /// Estimate the number of steps needed based on goal complexity.
    static sal_Int32 estimateSteps(const OUString& goal);

    /// Convert CanvasDocType to provider surface string.
    static OUString docTypeToSurface(CanvasDocType t);

    CanvasSession m_session;
};

} // namespace kqoffice::ai::canvas

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
