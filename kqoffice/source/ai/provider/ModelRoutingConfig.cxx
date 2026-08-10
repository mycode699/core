/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Five-slot user surface (primary / light / agent / plan / review).
 * Legacy keys smallFastModel / subagentModel / explore / general / team / guide
 * are accepted on load and expanded via expandUserSlots().
 */

#include "ModelRoutingConfig.hxx"

#include <osl/file.hxx>
#include <rtl/string.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace kqoffice::ai
{
namespace
{
OUString envOrEmpty(const char* name)
{
    const char* p = std::getenv(name);
    if (!p || !*p)
        return OUString();
    return OUString::fromUtf8(p);
}

OUString readFileUtf8(const OUString& rSystemPath)
{
    std::ifstream in(OUStringToOString(rSystemPath, RTL_TEXTENCODING_UTF8).getStr());
    if (!in)
        return OUString();
    std::ostringstream ss;
    ss << in.rdbuf();
    return OUString::fromUtf8(ss.str().c_str());
}

bool writeFileUtf8(const OUString& rSystemPath, const std::string& body)
{
    std::ofstream out(OUStringToOString(rSystemPath, RTL_TEXTENCODING_UTF8).getStr(),
                      std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << body;
    return static_cast<bool>(out);
}

/// Extract "key":"value" string fields with a linear scan (routing files are small).
OUString jsonStringField(const OUString& rJson, std::u16string_view key)
{
    OUString needle = u"\""_ustr + OUString(key) + u"\""_ustr;
    sal_Int32 pos = rJson.indexOf(needle);
    if (pos < 0)
        return OUString();
    pos = rJson.indexOf(u':', pos + needle.getLength());
    if (pos < 0)
        return OUString();
    // skip whitespace
    while (pos + 1 < rJson.getLength()
           && (rJson[pos + 1] == u' ' || rJson[pos + 1] == u'\t' || rJson[pos + 1] == u'\n'
               || rJson[pos + 1] == u'\r'))
        ++pos;
    if (pos + 1 >= rJson.getLength() || rJson[pos + 1] != u'"')
        return OUString();
    sal_Int32 start = pos + 2;
    OUStringBuffer buf;
    for (sal_Int32 i = start; i < rJson.getLength(); ++i)
    {
        const sal_Unicode c = rJson[i];
        if (c == u'\\')
        {
            if (i + 1 >= rJson.getLength())
                break;
            const sal_Unicode n = rJson[++i];
            if (n == u'"' || n == u'\\' || n == u'/')
                buf.append(n);
            else if (n == u'n')
                buf.append(u'\n');
            else if (n == u't')
                buf.append(u'\t');
            else
                buf.append(n);
            continue;
        }
        if (c == u'"')
            break;
        buf.append(c);
    }
    return buf.makeStringAndClear().trim();
}

void applyEnvOverrides(ModelRoutingSnapshot& r)
{
    auto setIf = [](OUString& field, const char* envName) {
        OUString v = envOrEmpty(envName);
        if (!v.isEmpty())
            field = v;
    };
    // Five-slot canonical env names
    setIf(r.primaryModel, "KQOFFICE_AI_PRIMARY_MODEL");
    setIf(r.primaryModel, "KQOFFICE_AI_MODEL"); // alias
    setIf(r.lightModel, "KQOFFICE_AI_LIGHT_MODEL");
    setIf(r.agentModel, "KQOFFICE_AI_AGENT_MODEL");
    setIf(r.planModel, "KQOFFICE_AI_PLAN_MODEL");
    setIf(r.reviewModel, "KQOFFICE_AI_REVIEW_MODEL");
    setIf(r.visionModel, "KQOFFICE_AI_VISION_MODEL");
    // Legacy env aliases → same five slots
    setIf(r.lightModel, "KQOFFICE_AI_SMALL_FAST_MODEL");
    setIf(r.agentModel, "KQOFFICE_AI_SUBAGENT_MODEL");
    setIf(r.planModel, "KQOFFICE_AI_GUIDE_MODEL"); // guide → plan
    setIf(r.agentModel, "KQOFFICE_AI_EXPLORE_MODEL");
    setIf(r.agentModel, "KQOFFICE_AI_GENERAL_MODEL");
    setIf(r.agentModel, "KQOFFICE_AI_TEAM_MODEL");
    setIf(r.backend, "KQOFFICE_AI_BACKEND");
    setIf(r.baseUrl, "KQOFFICE_AI_BASE_URL");
}

void appendJsonString(OUStringBuffer& b, std::u16string_view key, const OUString& value)
{
    b.append(u"  \"");
    b.append(key);
    b.append(u"\": \"");
    for (sal_Int32 i = 0; i < value.getLength(); ++i)
    {
        const sal_Unicode c = value[i];
        if (c == u'\\' || c == u'"')
            b.append(u'\\');
        b.append(c);
    }
    b.append(u"\"");
}
} // namespace

OUString defaultModelRoutingConfigPath()
{
    OUString overridePath = envOrEmpty("KQOFFICE_AI_ROUTING");
    if (!overridePath.isEmpty())
        return overridePath;

    // Include AiPaths via ModelRoles-compatible layout: prefer env HOME/APPDATA.
#if defined(_WIN32)
    const char* app = std::getenv("APPDATA");
    if (app && *app)
        return OUString::fromUtf8(app) + u"/kqoffice/model-routing.json"_ustr;
#endif
    const char* home = std::getenv("HOME");
    if (home && *home)
        return OUString::fromUtf8(home) + u"/.config/kqoffice/model-routing.json"_ustr;
#if defined(_WIN32)
    const char* up = std::getenv("USERPROFILE");
    if (up && *up)
        return OUString::fromUtf8(up) + u"/.config/kqoffice/model-routing.json"_ustr;
#endif
    return OUString();
}

ModelRoutingSnapshot parseModelRoutingJson(const OUString& rJson)
{
    ModelRoutingSnapshot s;
    if (rJson.isEmpty())
        return s;
    s.primaryModel = jsonStringField(rJson, u"primaryModel");
    if (s.primaryModel.isEmpty())
        s.primaryModel = jsonStringField(rJson, u"main"); // legacy alias

    // Five-slot canonical keys
    s.lightModel = jsonStringField(rJson, u"lightModel");
    s.agentModel = jsonStringField(rJson, u"agentModel");
    s.planModel = jsonStringField(rJson, u"planModel");
    s.reviewModel = jsonStringField(rJson, u"reviewModel");
    s.visionModel = jsonStringField(rJson, u"visionModel");

    // Legacy aliases (Clavue 9-slot / earlier kqoffice)
    s.smallFastModel = jsonStringField(rJson, u"smallFastModel");
    s.subagentModel = jsonStringField(rJson, u"subagentModel");
    s.exploreModel = jsonStringField(rJson, u"exploreModel");
    s.generalModel = jsonStringField(rJson, u"generalModel");
    s.teamModel = jsonStringField(rJson, u"teamModel");
    s.guideModel = jsonStringField(rJson, u"guideModel");

    // Prefer canonical; fill from legacy if empty
    if (s.lightModel.isEmpty())
        s.lightModel = s.smallFastModel;
    if (s.agentModel.isEmpty())
        s.agentModel = s.subagentModel;
    if (s.planModel.isEmpty() && !s.guideModel.isEmpty())
        s.planModel = s.guideModel;
    if (s.agentModel.isEmpty() && !s.exploreModel.isEmpty())
        s.agentModel = s.exploreModel;
    if (s.agentModel.isEmpty() && !s.generalModel.isEmpty())
        s.agentModel = s.generalModel;
    if (s.agentModel.isEmpty() && !s.teamModel.isEmpty())
        s.agentModel = s.teamModel;

    s.backend = jsonStringField(rJson, u"backend");
    s.baseUrl = jsonStringField(rJson, u"baseUrl");
    if (s.backend.isEmpty())
        s.backend = u"ollama"_ustr;
    return s;
}

ModelRoutingSnapshot loadModelRoutingSnapshot()
{
    ModelRoutingSnapshot s;
    const OUString path = defaultModelRoutingConfigPath();
    if (!path.isEmpty())
    {
        const OUString body = readFileUtf8(path);
        if (!body.isEmpty())
            s = parseModelRoutingJson(body);
    }
    if (s.backend.isEmpty())
        s.backend = u"ollama"_ustr;
    applyEnvOverrides(s);
    // Alias-only: prefer light/agent over legacy smallFast/subagent for UI/display.
    // Do NOT fill empty slots with primary here — that happens in resolveModelForRole.
    if (s.lightModel.isEmpty() && !s.smallFastModel.isEmpty())
        s.lightModel = s.smallFastModel;
    if (s.agentModel.isEmpty() && !s.subagentModel.isEmpty())
        s.agentModel = s.subagentModel;
    if (s.smallFastModel.isEmpty())
        s.smallFastModel = s.lightModel;
    if (s.subagentModel.isEmpty())
        s.subagentModel = s.agentModel;
    return s;
}

OUString serializeModelRoutingJson(const ModelRoutingSnapshot& r)
{
    // Persist only the five user-facing slots (+ connection). Leave empty
    // slots empty so runtime can fall back to primary without writing noise.
    OUString light = r.lightModel;
    if (light.isEmpty())
        light = r.smallFastModel;
    OUString agent = r.agentModel;
    if (agent.isEmpty())
        agent = r.subagentModel;

    OUStringBuffer b;
    b.append(u"{\n");
    b.append(u"  \"schema_version\": \"v2-five-slot\",\n");
    appendJsonString(b, u"backend", r.backend.isEmpty() ? u"ollama"_ustr : r.backend);
    b.append(u",\n");
    appendJsonString(b, u"baseUrl", r.baseUrl);
    b.append(u",\n");
    appendJsonString(b, u"primaryModel", r.primaryModel);
    b.append(u",\n");
    appendJsonString(b, u"lightModel", light);
    b.append(u",\n");
    appendJsonString(b, u"agentModel", agent);
    b.append(u",\n");
    appendJsonString(b, u"planModel", r.planModel);
    b.append(u",\n");
    appendJsonString(b, u"reviewModel", r.reviewModel);
    b.append(u",\n");
    appendJsonString(b, u"visionModel", r.visionModel);
    b.append(u"\n}\n");
    return b.makeStringAndClear();
}

bool ensureDefaultModelRoutingTemplate()
{
    const OUString path = defaultModelRoutingConfigPath();
    if (path.isEmpty())
        return false;

    // Create parent directory ~/.config/kqoffice
    const sal_Int32 slash = path.lastIndexOf(u'/');
    if (slash > 0)
    {
        const OUString dir = path.copy(0, slash);
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(dir, dirUrl) == osl::FileBase::E_None)
            osl::Directory::createPath(dirUrl);
    }

    // Do not overwrite user config
    std::ifstream exists(OUStringToOString(path, RTL_TEXTENCODING_UTF8).getStr());
    if (exists.good())
        return true;

    ModelRoutingSnapshot demo;
    demo.backend = u"ollama"_ustr;
    demo.baseUrl = u"http://127.0.0.1:11434"_ustr;
    // Leave model ids empty → runtime picks first installed Ollama model.
    // Recommended: set primary (main/fallback), light (summarize/concurrent),
    // agent (cowork/subagent), plan (planner/judge), review (reviewer).
    const OUString json = serializeModelRoutingJson(demo);
    return writeFileUtf8(path, std::string(OUStringToOString(json, RTL_TEXTENCODING_UTF8)));
}

bool saveModelRoutingSnapshot(const ModelRoutingSnapshot& rRouting)
{
    ensureDefaultModelRoutingTemplate();
    const OUString path = defaultModelRoutingConfigPath();
    if (path.isEmpty())
        return false;

    const sal_Int32 slash = path.lastIndexOf(u'/');
    if (slash > 0)
    {
        const OUString dir = path.copy(0, slash);
        OUString dirUrl;
        if (osl::FileBase::getFileURLFromSystemPath(dir, dirUrl) == osl::FileBase::E_None)
            osl::Directory::createPath(dirUrl);
    }

    ModelRoutingSnapshot toSave = rRouting;
    if (toSave.backend.isEmpty())
        toSave.backend = u"ollama"_ustr;
    // Mirror light↔smallFast and agent↔subagent before write so expand is stable.
    if (toSave.lightModel.isEmpty() && !toSave.smallFastModel.isEmpty())
        toSave.lightModel = toSave.smallFastModel;
    if (toSave.agentModel.isEmpty() && !toSave.subagentModel.isEmpty())
        toSave.agentModel = toSave.subagentModel;
    const OUString json = serializeModelRoutingJson(toSave);
    return writeFileUtf8(path, std::string(OUStringToOString(json, RTL_TEXTENCODING_UTF8)));
}

OUString modelRoutingConfigPathForDisplay()
{
    return defaultModelRoutingConfigPath();
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
