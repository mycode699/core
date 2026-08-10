# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#
# This file is part of the 可圈office project (V2 W1: Provider Runtime).
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

$(eval $(call gb_Library_Library,kqoffice_ai))

$(eval $(call gb_Library_set_include,kqoffice_ai,\
    -I$(SRCDIR)/kqoffice/source/ai/canvas \
    -I$(SRCDIR)/kqoffice/source/ai/chat \
    -I$(SRCDIR)/kqoffice/source/ai/control \
    -I$(SRCDIR)/kqoffice/source/ai/cowork \
    -I$(SRCDIR)/kqoffice/source/ai/filemgr \
    -I$(SRCDIR)/kqoffice/source/ai/i18n \
    -I$(SRCDIR)/kqoffice/source/ai/mesh \
    -I$(SRCDIR)/kqoffice/source/ai/provider \
    -I$(SRCDIR)/kqoffice/source/ai/workbench \
    -I$(SRCDIR)/kqoffice/source/ai/notebook \
    -I$(SRCDIR)/kqoffice/source/ai/vault \
    -I$(SRCDIR)/kqoffice/source/ai/control \
    $$(INCLUDE) \
))

$(eval $(call gb_Library_use_sdk_api,kqoffice_ai))

$(eval $(call gb_Library_use_libraries,kqoffice_ai,\
    comphelper \
    cppu \
    cppuhelper \
    sal \
    salhelper \
))

$(eval $(call gb_Library_set_componentfile,kqoffice_ai,kqoffice/util/kqoffice_ai,services))

$(eval $(call gb_Library_add_exception_objects,kqoffice_ai,\
    kqoffice/source/ai/provider/AgentStepRunner \
    kqoffice/source/ai/provider/EvidenceRecorder \
    kqoffice/source/ai/provider/MembershipClient \
    kqoffice/source/ai/provider/ModelRoles \
    kqoffice/source/ai/provider/ModelRoutingConfig \
    kqoffice/source/ai/provider/OllamaAdapter \
    kqoffice/source/ai/provider/OpenAICompatibleAdapter \
    kqoffice/source/ai/provider/Provider \
    kqoffice/source/ai/provider/ProviderStreamHelper \
    kqoffice/source/ai/provider/RuntimePlanStub \
    kqoffice/source/ai/provider/ServiceModePolicy \
    kqoffice/source/ai/cowork/AgentDelegation \
    kqoffice/source/ai/cowork/CoworkUiBridge \
    kqoffice/source/ai/cowork/ScheduledTask \
    kqoffice/source/ai/cowork/ScheduledTaskDispatcher \
    kqoffice/source/ai/cowork/TaskNativeOsNotificationBackend \
    kqoffice/source/ai/cowork/TaskQueue \
    kqoffice/source/ai/cowork/TaskOsNotificationBridge \
    kqoffice/source/ai/cowork/TaskReviewBridge \
    kqoffice/source/ai/cowork/TaskRunner \
    kqoffice/source/ai/cowork/TaskScheduler \
    kqoffice/source/ai/cowork/TaskStore \
    kqoffice/source/ai/chat/AgentChatContextBuilder \
    kqoffice/source/ai/chat/AgentChatDiffApplier \
    kqoffice/source/ai/chat/AgentChatDiffExtractor \
    kqoffice/source/ai/chat/AgentChatMentionResolver \
    kqoffice/source/ai/chat/AgentChatSelectionCapture \
    kqoffice/source/ai/chat/AgentChatStreamingClient \
    kqoffice/source/ai/chat/DocumentAIApply \
    kqoffice/source/ai/chat/DocumentAIContext \
    kqoffice/source/ai/chat/DocumentAIFormulaDryRun \
    kqoffice/source/ai/chat/DocumentAIMCPTools \
    kqoffice/source/ai/chat/DocumentAIMCPStdio \
    kqoffice/source/ai/chat/DocumentAIVerify \
    kqoffice/source/ai/chat/DocumentAITaskBootstrap \
    kqoffice/source/ai/chat/DocumentAIWorkPlan \
    kqoffice/source/ai/chat/DocumentAIRewriteMemory \
    kqoffice/source/ai/chat/DocumentAIDocumentTools \
    kqoffice/source/ai/chat/DocumentAILocalRag \
    kqoffice/source/ai/chat/DocumentAIMaterialReader \
    kqoffice/source/ai/chat/DocumentAIInputPrefs \
    kqoffice/source/ai/chat/DocumentAIScreenCapture \
    kqoffice/source/ai/chat/DocumentAIVisionEvidence \
    kqoffice/source/ai/chat/DocumentAIEnterpriseConnectors \
    kqoffice/source/ai/chat/DocumentAIVoiceInput \
    kqoffice/source/ai/chat/DocumentAIScenarioStore \
    kqoffice/source/ai/chat/DocumentAIScenarios \
    kqoffice/source/ai/control/PermissionCenter \
    kqoffice/source/ai/control/PermissionGrant \
    kqoffice/source/ai/control/ResourceBudgetWatchdog \
    kqoffice/source/ai/control/AiResourceEnvelope \
    kqoffice/source/ai/control/SafeRestore \
    kqoffice/source/ai/control/SessionStore \
    kqoffice/source/ai/control/SurfaceLifecycleManager \
    kqoffice/source/ai/mesh/WorkspaceAgentMesh \
    kqoffice/source/ai/mesh/WorkspaceAgentMeshOrchestrator \
    kqoffice/source/ai/mesh/WorkspaceMeshTaskQueue \
    kqoffice/source/ai/mesh/WorkspaceSupervisorAgentAPI \
    kqoffice/source/ai/canvas/AICanvasEntryPoint \
    kqoffice/source/ai/canvas/AICanvasIntegration \
    kqoffice/source/ai/canvas/AICanvasMode \
    kqoffice/source/ai/canvas/AICanvasUI \
    kqoffice/source/ai/filemgr/AIFileManager \
    kqoffice/source/ai/filemgr/AIFileSearchUI \
    kqoffice/source/ai/filemgr/BatchJob \
    kqoffice/source/ai/workbench/WorkTelemetryStore \
    kqoffice/source/ai/notebook/LocalNotebookStore \
    kqoffice/source/ai/notebook/LocalSpeechHub \
    kqoffice/source/ai/notebook/MediaTranscriptService \
    kqoffice/source/ai/notebook/NotebookMaterialStore \
    kqoffice/source/ai/notebook/NotebookProjectStore \
    kqoffice/source/ai/notebook/NotebookStudioPipeline \
    kqoffice/source/ai/vault/VaultStore \
    kqoffice/source/ai/vault/VaultManager \
    kqoffice/source/ai/vault/VaultIngest \
    kqoffice/source/ai/vault/VaultCompile \
    kqoffice/source/ai/vault/VaultLint \
    kqoffice/source/ai/vault/VaultPack \
))

ifeq ($(OS),MACOSX)
$(eval $(call gb_Library_add_cxxflags,kqoffice_ai,\
    $(gb_OBJCXXFLAGS) \
))
$(eval $(call gb_Library_add_objcxxobjects,kqoffice_ai,\
    kqoffice/source/ai/cowork/MacTaskNativeOsNotificationBackend \
))
$(eval $(call gb_Library_use_system_darwin_frameworks,kqoffice_ai,\
    Foundation \
))
endif

ifeq ($(OS),WNT)
$(eval $(call gb_Library_add_exception_objects,kqoffice_ai,\
    kqoffice/source/ai/cowork/WindowsTaskNativeOsNotificationBackend \
))
$(eval $(call gb_Library_use_system_win32_libs,kqoffice_ai,\
    shell32 \
))
endif

# vim: set noet sw=4 ts=4:
