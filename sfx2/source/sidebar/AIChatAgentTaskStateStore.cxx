/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the KQOffice project (V3 W6/M5: agent task state store).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AIChatAgentTaskStateStore.hxx"

#include "AIChatKnowledgeIndexStore.hxx"

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>
#include <unotools/pathoptions.hxx>

#include <algorithm>
#include <string_view>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
constexpr OUStringLiteral AGENT_TASK_STATE_DIR_NAME = u"kqoffice-v3-ai-agent-task-state";
constexpr OUStringLiteral TASK_STATE_FILE_NAME = u"task-states.tsv";
constexpr OUStringLiteral STEP_RESULT_FILE_NAME = u"step-results.tsv";
constexpr sal_uInt64 MAX_AGENT_TASK_STATE_BYTES = 1024 * 1024;

OUString EnsureNoTrailingSlash(OUString sUrl)
{
    while (sUrl.endsWith(u"/"))
        sUrl = sUrl.copy(0, sUrl.getLength() - 1);
    return sUrl;
}

OUString EscapeField(const OUString& rValue)
{
    OUStringBuffer aBuffer;
    for (sal_Int32 i = 0; i < rValue.getLength(); ++i)
    {
        const sal_Unicode c = rValue[i];
        switch (c)
        {
            case '\\':
                aBuffer.append(u"\\\\"_ustr);
                break;
            case '\n':
                aBuffer.append(u"\\n"_ustr);
                break;
            case '\r':
                aBuffer.append(u"\\r"_ustr);
                break;
            case '\t':
                aBuffer.append(u"\\t"_ustr);
                break;
            default:
                aBuffer.append(c);
                break;
        }
    }
    return aBuffer.makeStringAndClear();
}

OUString UnescapeField(std::u16string_view aValue)
{
    OUStringBuffer aBuffer;
    for (size_t i = 0; i < aValue.size(); ++i)
    {
        if (aValue[i] != u'\\' || i + 1 >= aValue.size())
        {
            aBuffer.append(aValue[i]);
            continue;
        }

        const char16_t cNext = aValue[++i];
        switch (cNext)
        {
            case u'n':
                aBuffer.append(u'\n');
                break;
            case u'r':
                aBuffer.append(u'\r');
                break;
            case u't':
                aBuffer.append(u'\t');
                break;
            case u'\\':
                aBuffer.append(u'\\');
                break;
            default:
                aBuffer.append(cNext);
                break;
        }
    }
    return aBuffer.makeStringAndClear();
}

OUString BoolToField(bool bValue) { return bValue ? u"true"_ustr : u"false"_ustr; }

bool FieldToBool(const OUString& rValue) { return rValue == u"true"_ustr; }

OUString JoinList(const std::vector<OUString>& rValues)
{
    OUStringBuffer aBuffer;
    for (size_t i = 0; i < rValues.size(); ++i)
    {
        if (i > 0)
            aBuffer.append(u',');
        aBuffer.append(rValues[i]);
    }
    return aBuffer.makeStringAndClear();
}

std::vector<OUString> SplitList(const OUString& rValue)
{
    std::vector<OUString> aValues;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sValue = rValue.getToken(0, ',', nIndex);
        if (!sValue.isEmpty())
            aValues.push_back(sValue);
    }
    return aValues;
}

bool AppendUtf8Line(const OUString& rUrl, const OUString& rLine)
{
    const OString sUtf8 = OUStringToOString(rLine, RTL_TEXTENCODING_UTF8);
    osl::File aFile(rUrl);
    osl::FileBase::RC eError = aFile.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (eError != osl::FileBase::E_None)
        eError = aFile.open(osl_File_OpenFlag_Write);
    if (eError != osl::FileBase::E_None)
        return false;

    sal_uInt64 nSize = 0;
    aFile.getSize(nSize);
    aFile.setPos(osl_Pos_Absolut, nSize);

    sal_uInt64 nWritten = 0;
    const bool bWritten
        = aFile.write(sUtf8.getStr(), sUtf8.getLength(), nWritten) == osl::FileBase::E_None
          && nWritten == static_cast<sal_uInt64>(sUtf8.getLength());
    aFile.close();
    return bWritten;
}

bool ReadUtf8File(const OUString& rUrl, OString& rContent)
{
    osl::File aFile(rUrl);
    if (aFile.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return false;

    sal_uInt64 nSize = 0;
    if (aFile.getSize(nSize) != osl::FileBase::E_None || nSize > MAX_AGENT_TASK_STATE_BYTES)
    {
        aFile.close();
        return false;
    }

    std::vector<char> aBuffer(static_cast<size_t>(nSize));
    sal_uInt64 nRead = 0;
    if (nSize > 0
        && aFile.read(aBuffer.data(), nSize, nRead) != osl::FileBase::E_None)
    {
        aFile.close();
        return false;
    }
    aFile.close();

    if (nRead != nSize)
        return false;

    rContent = OString(aBuffer.data(), static_cast<sal_Int32>(aBuffer.size()));
    return true;
}

bool IsLowerHex(const OUString& rValue, sal_Int32 nLength)
{
    if (rValue.getLength() != nLength)
        return false;
    for (sal_Int32 i = 0; i < rValue.getLength(); ++i)
    {
        const sal_Unicode c = rValue[i];
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')))
            return false;
    }
    return true;
}

bool ContainsString(const std::vector<OUString>& rValues, const OUString& rNeedle)
{
    return std::find(rValues.begin(), rValues.end(), rNeedle) != rValues.end();
}

bool ContainsDuplicateString(const std::vector<OUString>& rValues)
{
    for (auto it = rValues.begin(); it != rValues.end(); ++it)
    {
        if (std::find(it + 1, rValues.end(), *it) != rValues.end())
            return true;
    }
    return false;
}

bool ParseTaskStateLine(const OUString& rLine, AIChatAgentTaskStateEntry& rEntry)
{
    std::vector<OUString> aFields;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sField = rLine.getToken(0, '\t', nIndex);
        aFields.push_back(UnescapeField(sField));
    }

    if (aFields.size() != 34 || aFields[0].isEmpty())
        return false;

    rEntry.TaskId = aFields[0];
    rEntry.SchemaVersion = aFields[1];
    rEntry.UpdatedAt = aFields[2];
    rEntry.OwnerSurface = aFields[3];
    rEntry.State = aFields[4];
    rEntry.CurrentStepIndex = aFields[5].toInt32();
    rEntry.MaxSteps = aFields[6].toInt32();
    rEntry.CompletedSteps = aFields[7].toInt32();
    rEntry.FailedSteps = aFields[8].toInt32();
    rEntry.CancelledSteps = aFields[9].toInt32();
    rEntry.UsesV2AsyncCowork = FieldToBool(aFields[10]);
    rEntry.TaskKind = aFields[11];
    rEntry.CoworkTaskState = aFields[12];
    rEntry.ApprovalMode = aFields[13];
    rEntry.WholeTaskApprovalRequired = FieldToBool(aFields[14]);
    rEntry.PerStepApprovalSupported = FieldToBool(aFields[15]);
    rEntry.SoftCancelSupported = FieldToBool(aFields[16]);
    rEntry.HardCancelSupported = FieldToBool(aFields[17]);
    rEntry.UserDecisionRequired = FieldToBool(aFields[18]);
    rEntry.MainDocumentUnchangedOnFailure = FieldToBool(aFields[19]);
    rEntry.MergeTarget = aFields[20];
    rEntry.MergeRequiresApproval = FieldToBool(aFields[21]);
    rEntry.RequiresApplyPlanRuntimeValidation = FieldToBool(aFields[22]);
    rEntry.AuditLogRequired = FieldToBool(aFields[23]);
    rEntry.RequiredForEveryStep = SplitList(aFields[24]);
    rEntry.TaskEvidenceIds = SplitList(aFields[25]);
    rEntry.CheckpointId = aFields[26];
    rEntry.EvidenceCompleteCheckpoint = FieldToBool(aFields[27]);
    rEntry.DocumentHashReference = aFields[28];
    rEntry.ShadowSnapshotRef = aFields[29];
    rEntry.AuditReplayRef = aFields[30];
    rEntry.ResumeRequiresUserConfirmation = FieldToBool(aFields[31]);
    if (aFields[33] != u"metadata-only"_ustr)
        return false;
    return true;
}

bool ParseStepResultLine(const OUString& rLine, AIChatAgentStepResultEntry& rEntry)
{
    std::vector<OUString> aFields;
    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sField = rLine.getToken(0, '\t', nIndex);
        aFields.push_back(UnescapeField(sField));
    }

    if (aFields.size() != 25 || aFields[0].isEmpty())
        return false;

    rEntry.ResultId = aFields[0];
    rEntry.SchemaVersion = aFields[1];
    rEntry.TaskId = aFields[2];
    rEntry.StepIndex = aFields[3].toInt32();
    rEntry.Kind = aFields[4];
    rEntry.Status = aFields[5];
    rEntry.StartedAt = aFields[6];
    rEntry.FinishedAt = aFields[7];
    rEntry.OutputKind = aFields[8];
    rEntry.OutputSchemaRef = aFields[9];
    rEntry.OutputRefId = aFields[10];
    rEntry.StoresDocumentContent = FieldToBool(aFields[11]);
    rEntry.ApplyPlanRuntimeValidated = FieldToBool(aFields[12]);
    rEntry.SandboxMode = aFields[13];
    rEntry.ShadowBranchId = aFields[14];
    rEntry.MainDocumentUnchanged = FieldToBool(aFields[15]);
    rEntry.FailureIsolation = aFields[16];
    rEntry.PolicyPreflight = FieldToBool(aFields[17]);
    rEntry.PolicyAuditLog = FieldToBool(aFields[18]);
    rEntry.PolicyDecision = aFields[19];
    rEntry.RequiredEvidence = SplitList(aFields[20]);
    rEntry.EvidenceIds = SplitList(aFields[21]);
    rEntry.FailureCode = aFields[22];
    rEntry.FailureRecoverable = FieldToBool(aFields[23]);
    rEntry.RetryAllowed = FieldToBool(aFields[24]);
    return true;
}
}

AIChatAgentTaskStateStore::AIChatAgentTaskStateStore()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);

    m_sStorageRootUrl = EnsureNoTrailingSlash(sRoot) + u"/"_ustr + AGENT_TASK_STATE_DIR_NAME;
    osl::Directory::createPath(m_sStorageRootUrl);
    m_sTaskStateUrl = m_sStorageRootUrl + u"/"_ustr + TASK_STATE_FILE_NAME;
    m_sStepResultUrl = m_sStorageRootUrl + u"/"_ustr + STEP_RESULT_FILE_NAME;
}

bool AIChatAgentTaskStateStore::IsTaskIdAllowed(const OUString& rTaskId)
{
    return rTaskId.startsWith(u"agt-"_ustr) && IsLowerHex(rTaskId.copy(4), 16);
}

bool AIChatAgentTaskStateStore::IsStepResultIdAllowed(const OUString& rResultId)
{
    return rResultId.startsWith(u"agsr-"_ustr) && IsLowerHex(rResultId.copy(5), 16);
}

bool AIChatAgentTaskStateStore::IsShadowBranchIdAllowed(const OUString& rBranchId)
{
    return rBranchId.startsWith(u"shadow-"_ustr) && IsLowerHex(rBranchId.copy(7), 16);
}

bool AIChatAgentTaskStateStore::IsEvidenceIdAllowed(const OUString& rEvidenceId)
{
    return rEvidenceId.startsWith(u"ev-"_ustr) && IsLowerHex(rEvidenceId.copy(3), 16);
}

bool AIChatAgentTaskStateStore::IsOutputRefAllowed(const OUString& rRefId)
{
    const std::vector<OUString> aPrefixes = { u"conn-"_ustr, u"kbr-"_ustr, u"agsr-"_ustr,
                                              u"aprt-"_ustr, u"appr-"_ustr, u"none-"_ustr };
    for (const OUString& rPrefix : aPrefixes)
    {
        if (rRefId.startsWith(rPrefix) && IsLowerHex(rRefId.copy(rPrefix.getLength()), 16))
            return true;
    }
    return false;
}

bool AIChatAgentTaskStateStore::IsTaskStateAllowed(const OUString& rState)
{
    return rState == u"pending"_ustr || rState == u"running"_ustr
           || rState == u"awaiting-review"_ustr || rState == u"applied"_ustr
           || rState == u"failed"_ustr || rState == u"cancelled"_ustr;
}

bool AIChatAgentTaskStateStore::IsCoworkTaskStateAllowed(const OUString& rState)
{
    return rState == u"pending"_ustr || rState == u"running"_ustr
           || rState == u"awaiting-review"_ustr || rState == u"applied"_ustr
           || rState == u"failed"_ustr || rState == u"cancelled"_ustr;
}

bool AIChatAgentTaskStateStore::IsStepResultStatusAllowed(const OUString& rStatus)
{
    return rStatus == u"completed"_ustr || rStatus == u"failed"_ustr
           || rStatus == u"cancelled"_ustr;
}

bool AIChatAgentTaskStateStore::IsTerminalState(const OUString& rState)
{
    return rState == u"applied"_ustr || rState == u"failed"_ustr
           || rState == u"cancelled"_ustr;
}

bool AIChatAgentTaskStateStore::IsAllowedTransition(const OUString& rFromState,
                                                    const OUString& rToState)
{
    if (!IsTaskStateAllowed(rFromState) || !IsTaskStateAllowed(rToState)
        || IsTerminalState(rFromState))
        return false;
    if (rFromState == rToState)
        return true;
    if (rFromState == u"pending"_ustr)
        return rToState == u"running"_ustr || rToState == u"cancelled"_ustr;
    if (rFromState == u"running"_ustr)
        return rToState == u"awaiting-review"_ustr || rToState == u"failed"_ustr
               || rToState == u"cancelled"_ustr;
    if (rFromState == u"awaiting-review"_ustr)
        return rToState == u"applied"_ustr || rToState == u"failed"_ustr
               || rToState == u"cancelled"_ustr;
    return false;
}

bool AIChatAgentTaskStateStore::IsBaseEvidenceComplete(const std::vector<OUString>& rEvidence)
{
    return !ContainsDuplicateString(rEvidence)
           && ContainsString(rEvidence, u"policy-decision"_ustr)
           && ContainsString(rEvidence, u"evidence-record"_ustr)
           && ContainsString(rEvidence, u"audit-log-entry"_ustr);
}

bool AIChatAgentTaskStateStore::IsTaskEvidenceComplete(
    const AIChatAgentTaskStateEntry& rEntry)
{
    if (!IsBaseEvidenceComplete(rEntry.RequiredForEveryStep) || rEntry.TaskEvidenceIds.empty()
        || ContainsDuplicateString(rEntry.TaskEvidenceIds))
        return false;
    for (const OUString& rEvidenceId : rEntry.TaskEvidenceIds)
    {
        if (!IsEvidenceIdAllowed(rEvidenceId))
            return false;
    }
    return true;
}

bool AIChatAgentTaskStateStore::IsStepResultAllowed(
    const AIChatAgentStepResultEntry& rEntry)
{
    if (!IsStepResultIdAllowed(rEntry.ResultId) || rEntry.SchemaVersion != u"v3-agent-step-result/0.1"_ustr
        || !IsTaskIdAllowed(rEntry.TaskId) || rEntry.StepIndex < 0 || rEntry.StepIndex > 24
        || !IsStepResultStatusAllowed(rEntry.Status) || !IsOutputRefAllowed(rEntry.OutputRefId)
        || rEntry.StoresDocumentContent || rEntry.SandboxMode != u"shadow-doc"_ustr
        || !IsShadowBranchIdAllowed(rEntry.ShadowBranchId) || !rEntry.MainDocumentUnchanged
        || rEntry.FailureIsolation != u"discard-step-branch"_ustr || !rEntry.PolicyPreflight
        || !rEntry.PolicyAuditLog || !IsBaseEvidenceComplete(rEntry.RequiredEvidence)
        || rEntry.EvidenceIds.empty() || ContainsDuplicateString(rEntry.EvidenceIds))
        return false;

    for (const OUString& rEvidenceId : rEntry.EvidenceIds)
    {
        if (!IsEvidenceIdAllowed(rEvidenceId))
            return false;
    }

    if (rEntry.Kind == u"patch"_ustr && !rEntry.ApplyPlanRuntimeValidated)
        return false;
    if (rEntry.Status == u"failed"_ustr)
        return rEntry.FailureCode != u"none"_ustr && rEntry.MainDocumentUnchanged;
    if (rEntry.Status == u"cancelled"_ustr)
        return rEntry.FailureCode == u"user-cancelled"_ustr;
    return true;
}

bool AIChatAgentTaskStateStore::IsTaskStateShapeAllowed(
    const AIChatAgentTaskStateEntry& rEntry)
{
    return IsTaskIdAllowed(rEntry.TaskId)
           && rEntry.SchemaVersion == u"v3-agent-task-state/0.1"_ustr
           && IsTaskStateAllowed(rEntry.State) && IsCoworkTaskStateAllowed(rEntry.CoworkTaskState)
           && rEntry.CoworkTaskState == rEntry.State && rEntry.CurrentStepIndex >= 0
           && rEntry.CurrentStepIndex <= 24 && rEntry.MaxSteps >= 1 && rEntry.MaxSteps <= 25
           && rEntry.UsesV2AsyncCowork && rEntry.TaskKind == u"agent-multistep"_ustr
           && (rEntry.ApprovalMode == u"whole-task"_ustr || rEntry.ApprovalMode == u"per-step"_ustr)
           && rEntry.WholeTaskApprovalRequired && rEntry.PerStepApprovalSupported
           && rEntry.SoftCancelSupported && rEntry.HardCancelSupported
           && rEntry.UserDecisionRequired && rEntry.MainDocumentUnchangedOnFailure
           && rEntry.MergeTarget == u"main-doc"_ustr && rEntry.MergeRequiresApproval
           && rEntry.RequiresApplyPlanRuntimeValidation && rEntry.AuditLogRequired
           && IsTaskEvidenceComplete(rEntry);
}

OUString AIChatAgentTaskStateStore::MakeStepResultId(const OUString& rTaskId,
                                                     sal_Int32 nStepIndex)
{
    return u"agsr-"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(rTaskId + u":step-result:"_ustr
                                                         + OUString::number(nStepIndex))
                 .copy(0, 16);
}

OUString AIChatAgentTaskStateStore::MakeCheckpointId(const OUString& rTaskId,
                                                     sal_Int32 nStepIndex)
{
    return u"checkpoint:"_ustr + rTaskId + u":"_ustr + OUString::number(nStepIndex);
}

OUString AIChatAgentTaskStateStore::MakeTaskHashReference(
    const AIChatAgentTaskStateEntry& rEntry)
{
    return u"sha256:"_ustr
           + AIChatKnowledgeIndexStore::MakeMetadataHash(
                 rEntry.TaskId + u":"_ustr + rEntry.State + u":"_ustr
                 + OUString::number(rEntry.CurrentStepIndex));
}

bool AIChatAgentTaskStateStore::RecordStepResult(
    const AIChatAgentStepResultEntry& rEntry) const
{
    if (!IsStepResultAllowed(rEntry))
        return false;

    const OUString sLine
        = EscapeField(rEntry.ResultId) + u"\t"_ustr + EscapeField(rEntry.SchemaVersion)
          + u"\t"_ustr + EscapeField(rEntry.TaskId) + u"\t"_ustr
          + EscapeField(OUString::number(rEntry.StepIndex)) + u"\t"_ustr
          + EscapeField(rEntry.Kind) + u"\t"_ustr + EscapeField(rEntry.Status) + u"\t"_ustr
          + EscapeField(rEntry.StartedAt) + u"\t"_ustr + EscapeField(rEntry.FinishedAt)
          + u"\t"_ustr + EscapeField(rEntry.OutputKind) + u"\t"_ustr
          + EscapeField(rEntry.OutputSchemaRef) + u"\t"_ustr
          + EscapeField(rEntry.OutputRefId) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.StoresDocumentContent)) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.ApplyPlanRuntimeValidated)) + u"\t"_ustr
          + EscapeField(rEntry.SandboxMode) + u"\t"_ustr
          + EscapeField(rEntry.ShadowBranchId) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.MainDocumentUnchanged)) + u"\t"_ustr
          + EscapeField(rEntry.FailureIsolation) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.PolicyPreflight)) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.PolicyAuditLog)) + u"\t"_ustr
          + EscapeField(rEntry.PolicyDecision) + u"\t"_ustr
          + EscapeField(JoinList(rEntry.RequiredEvidence)) + u"\t"_ustr
          + EscapeField(JoinList(rEntry.EvidenceIds)) + u"\t"_ustr
          + EscapeField(rEntry.FailureCode) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.FailureRecoverable)) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.RetryAllowed)) + u"\n"_ustr;
    return AppendUtf8Line(m_sStepResultUrl, sLine);
}

AIChatAgentTaskStateResult
AIChatAgentTaskStateStore::SaveTaskState(const AIChatAgentTaskStateEntry& rEntry) const
{
    return RecordTaskState(rEntry);
}

AIChatAgentTaskStateResult
AIChatAgentTaskStateStore::RecordTaskState(const AIChatAgentTaskStateEntry& rEntry) const
{
    AIChatAgentTaskStateResult aResult;
    aResult.State = rEntry;
    if (!IsTaskStateShapeAllowed(rEntry))
    {
        aResult.Message
            = u"agent-task-state-failed reason=invalid-task-state-shape metadata-only=true"_ustr
              + u" usesV2AsyncCowork=true mainDocumentUnchanged=true"_ustr
              + u" requiresApplyPlanRuntimeValidation=true evidence-complete-checkpoint=required"_ustr
              + u" resume-auto=false user-decision-required=true main-document-mutation=false"_ustr;
        return aResult;
    }

    const OUString sLine
        = EscapeField(rEntry.TaskId) + u"\t"_ustr + EscapeField(rEntry.SchemaVersion)
          + u"\t"_ustr + EscapeField(rEntry.UpdatedAt) + u"\t"_ustr
          + EscapeField(rEntry.OwnerSurface) + u"\t"_ustr + EscapeField(rEntry.State)
          + u"\t"_ustr + EscapeField(OUString::number(rEntry.CurrentStepIndex)) + u"\t"_ustr
          + EscapeField(OUString::number(rEntry.MaxSteps)) + u"\t"_ustr
          + EscapeField(OUString::number(rEntry.CompletedSteps)) + u"\t"_ustr
          + EscapeField(OUString::number(rEntry.FailedSteps)) + u"\t"_ustr
          + EscapeField(OUString::number(rEntry.CancelledSteps)) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.UsesV2AsyncCowork)) + u"\t"_ustr
          + EscapeField(rEntry.TaskKind) + u"\t"_ustr + EscapeField(rEntry.CoworkTaskState)
          + u"\t"_ustr + EscapeField(rEntry.ApprovalMode) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.WholeTaskApprovalRequired)) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.PerStepApprovalSupported)) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.SoftCancelSupported)) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.HardCancelSupported)) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.UserDecisionRequired)) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.MainDocumentUnchangedOnFailure)) + u"\t"_ustr
          + EscapeField(rEntry.MergeTarget) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.MergeRequiresApproval)) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.RequiresApplyPlanRuntimeValidation)) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.AuditLogRequired)) + u"\t"_ustr
          + EscapeField(JoinList(rEntry.RequiredForEveryStep)) + u"\t"_ustr
          + EscapeField(JoinList(rEntry.TaskEvidenceIds)) + u"\t"_ustr
          + EscapeField(rEntry.CheckpointId) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.EvidenceCompleteCheckpoint)) + u"\t"_ustr
          + EscapeField(rEntry.DocumentHashReference) + u"\t"_ustr
          + EscapeField(rEntry.ShadowSnapshotRef) + u"\t"_ustr
          + EscapeField(rEntry.AuditReplayRef) + u"\t"_ustr
          + EscapeField(BoolToField(rEntry.ResumeRequiresUserConfirmation)) + u"\t"_ustr
          + EscapeField(MakeTaskHashReference(rEntry)) + u"\t"_ustr
          + EscapeField(u"metadata-only"_ustr) + u"\n"_ustr;
    if (!AppendUtf8Line(m_sTaskStateUrl, sLine))
    {
        aResult.Message = u"agent-task-state-failed reason=state-write-failed"_ustr;
        return aResult;
    }

    aResult.Success = true;
    aResult.Message
        = u"agent-task-state-recorded task-id="_ustr + rEntry.TaskId + u" state="_ustr
          + rEntry.State + u" current-step="_ustr + OUString::number(rEntry.CurrentStepIndex)
          + u" maxSteps<=25 usesV2AsyncCowork=true taskKind=agent-multistep"_ustr
          + u" approval-mode="_ustr + rEntry.ApprovalMode
          + u" wholeTaskApprovalRequired=true perStepApprovalSupported=true"_ustr
          + u" softCancelSupported=true hardCancelSupported=true userDecisionRequired=true"_ustr
          + u" merge-target=main-doc requiresApproval=true"_ustr
          + u" requiresApplyPlanRuntimeValidation=true auditLogRequired=true"_ustr
          + u" evidence-complete-checkpoint="_ustr
          + BoolToField(rEntry.EvidenceCompleteCheckpoint)
          + u" checkpoint-id="_ustr + rEntry.CheckpointId
          + u" resume-auto=false resumeRequiresUserConfirmation=true"_ustr
          + u" mainDocumentUnchanged=true mainDocumentUnchangedOnFailure=true"_ustr
          + u" storesDocumentContent=false raw-output=false raw-step-result=false"_ustr
          + u" metadata-only=true main-document-mutation=false"_ustr;
    return aResult;
}

AIChatAgentTaskStateResult
AIChatAgentTaskStateStore::TransitionTaskState(const AIChatAgentTaskStateEntry& rCurrent,
                                               const OUString& rNewState,
                                               const OUString& rUpdatedAt,
                                               const OUString& rEvidenceId) const
{
    AIChatAgentTaskStateResult aResult;
    if (!IsAllowedTransition(rCurrent.State, rNewState) || !IsEvidenceIdAllowed(rEvidenceId))
    {
        aResult.Message
            = u"agent-task-transition-failed reason=invalid-transition-or-evidence"_ustr
              + u" forward-only-state-machine=true terminal-state-locked=true"_ustr
              + u" main-document-mutation=false"_ustr;
        return aResult;
    }

    AIChatAgentTaskStateEntry aNext = rCurrent;
    aNext.State = rNewState;
    aNext.CoworkTaskState = rNewState;
    aNext.UpdatedAt = rUpdatedAt;
    aNext.TaskEvidenceIds.push_back(rEvidenceId);
    return RecordTaskState(aNext);
}

AIChatAgentTaskStateResult
AIChatAgentTaskStateStore::RequestCancel(const AIChatAgentTaskStateEntry& rCurrent,
                                         const OUString& rCancelMode,
                                         const OUString& rUpdatedAt,
                                         const OUString& rEvidenceId) const
{
    AIChatAgentTaskStateResult aResult;
    if ((rCancelMode != u"soft-cancel"_ustr && rCancelMode != u"hard-cancel"_ustr)
        || !rCurrent.SoftCancelSupported || !rCurrent.HardCancelSupported
        || !rCurrent.UserDecisionRequired)
    {
        aResult.Message = u"agent-task-cancel-failed reason=cancel-policy-invalid"_ustr;
        return aResult;
    }
    AIChatAgentTaskStateResult aRecorded
        = TransitionTaskState(rCurrent, u"cancelled"_ustr, rUpdatedAt, rEvidenceId);
    if (aRecorded.Success)
        aRecorded.Message += u" cancel-mode="_ustr + rCancelMode
                             + u" cancel-request-evidence=true userDecisionRequired=true"_ustr;
    return aRecorded;
}

AIChatAgentTaskStateResult AIChatAgentTaskStateStore::ValidateResumeCheckpoint(
    const AIChatAgentTaskStateEntry& rEntry, const OUString& rCurrentDocumentHashReference,
    bool bUserConfirmed) const
{
    AIChatAgentTaskStateResult aResult;
    aResult.State = rEntry;
    if (!rEntry.EvidenceCompleteCheckpoint || rEntry.CheckpointId.isEmpty()
        || !bUserConfirmed || !rEntry.ResumeRequiresUserConfirmation
        || rEntry.DocumentHashReference != rCurrentDocumentHashReference
        || rEntry.ShadowSnapshotRef.isEmpty() || rEntry.AuditReplayRef.isEmpty()
        || !IsTaskEvidenceComplete(rEntry))
    {
        aResult.Message
            = u"agent-task-resume-failed reason=checkpoint-not-evidence-complete"_ustr
              + u" resumePoint=evidence-complete-checkpoint"_ustr
              + u" autoResumeAllowed=false staleCheckpointBehavior=fail-closed-user-visible"_ustr
              + u" requiresUserConfirmation=true requiresDocumentHashMatch=true"_ustr
              + u" requiresShadowSnapshot=true requiresAuditReplay=true"_ustr;
        return aResult;
    }

    aResult.Success = true;
    aResult.Message = u"agent-task-resume-allowed task-id="_ustr + rEntry.TaskId
                      + u" checkpoint-id="_ustr + rEntry.CheckpointId
                      + u" resumePoint=evidence-complete-checkpoint"_ustr
                      + u" autoResumeAllowed=false user-confirmed=true"_ustr
                      + u" document-hash-match=true shadow-snapshot=true audit-replay=true"_ustr;
    return aResult;
}

AIChatAgentTaskStateResult
AIChatAgentTaskStateStore::GetLatestTaskState(const OUString& rTaskId) const
{
    AIChatAgentTaskStateResult aResult;
    if (!IsTaskIdAllowed(rTaskId))
    {
        aResult.Message = u"agent-task-state-failed reason=task-id-invalid"_ustr;
        return aResult;
    }

    const std::vector<AIChatAgentTaskStateEntry> aEntries = LoadTaskStates();
    const auto it = std::find_if(aEntries.begin(), aEntries.end(),
                                 [&rTaskId](const AIChatAgentTaskStateEntry& rEntry) {
                                     return rEntry.TaskId == rTaskId;
                                 });
    if (it == aEntries.end())
    {
        aResult.Message = u"agent-task-state-failed reason=task-state-not-found"_ustr;
        return aResult;
    }

    aResult.Success = true;
    aResult.State = *it;
    aResult.Message = u"agent-task-state-loaded task-id="_ustr + rTaskId + u" state="_ustr
                      + it->State + u" metadata-only=true"_ustr;
    return aResult;
}

std::vector<AIChatAgentTaskStateEntry> AIChatAgentTaskStateStore::LoadTaskStates() const
{
    OString sContent;
    if (!ReadUtf8File(m_sTaskStateUrl, sContent) || sContent.isEmpty())
        return {};

    const OUString sUtf16 = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8);
    std::vector<AIChatAgentTaskStateEntry> aOrderedEntries;

    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sLine = sUtf16.getToken(0, '\n', nIndex);
        if (sLine.isEmpty())
            continue;

        AIChatAgentTaskStateEntry aEntry;
        if (!ParseTaskStateLine(sLine, aEntry) || !IsTaskStateShapeAllowed(aEntry))
            continue;

        auto it = std::find_if(aOrderedEntries.begin(), aOrderedEntries.end(),
                               [&aEntry](const AIChatAgentTaskStateEntry& rExisting) {
                                   return rExisting.TaskId == aEntry.TaskId;
                               });
        if (it == aOrderedEntries.end())
            aOrderedEntries.push_back(aEntry);
        else
            *it = aEntry;
    }

    std::reverse(aOrderedEntries.begin(), aOrderedEntries.end());
    return aOrderedEntries;
}

std::vector<AIChatAgentStepResultEntry>
AIChatAgentTaskStateStore::LoadStepResults(const OUString& rTaskId) const
{
    if (!IsTaskIdAllowed(rTaskId))
        return {};

    OString sContent;
    if (!ReadUtf8File(m_sStepResultUrl, sContent) || sContent.isEmpty())
        return {};

    const OUString sUtf16 = OStringToOUString(sContent, RTL_TEXTENCODING_UTF8);
    std::vector<AIChatAgentStepResultEntry> aEntries;

    sal_Int32 nIndex = 0;
    while (nIndex >= 0)
    {
        const OUString sLine = sUtf16.getToken(0, '\n', nIndex);
        if (sLine.isEmpty())
            continue;

        AIChatAgentStepResultEntry aEntry;
        if (ParseStepResultLine(sLine, aEntry) && aEntry.TaskId == rTaskId
            && IsStepResultAllowed(aEntry))
        {
            aEntries.push_back(aEntry);
        }
    }

    return aEntries;
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
