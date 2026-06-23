/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "RuntimePlanStub.hxx"

#include <rtl/ustring.hxx>
#include <rtl/ustrbuf.hxx>

namespace kqoffice::ai
{
namespace
{
void lcl_appendJsonEscaped(OUStringBuffer& rOut, std::u16string_view rText)
{
    for (size_t i = 0; i < rText.size(); ++i)
    {
        const sal_Unicode c = rText[i];
        switch (c)
        {
            case '"':
                rOut.append("\\\"");
                break;
            case '\\':
                rOut.append("\\\\");
                break;
            case '\n':
                rOut.append("\\n");
                break;
            case '\r':
                rOut.append("\\r");
                break;
            case '\t':
                rOut.append("\\t");
                break;
            default:
                rOut.append(c);
                break;
        }
    }
}
} // namespace

OUString buildStubRuntimePlanJson(const css::ai::ProviderRequest& req)
{
    OUString aParagraphId = req.context;
    if (aParagraphId.isEmpty())
        aParagraphId = u"swpara-1"_ustr;

    OUString aAfter = req.prompt;
    if (aAfter.isEmpty())
        aAfter = u"stub rewrite output"_ustr;

    OUStringBuffer aJson(512);
    aJson.append(
        u"{\n"
        u"  \"schema_version\": \"v2-w3-runtime-1\",\n"
        u"  \"plan_id\": \"ap-stub-runtime-001\",\n"
        u"  \"source_diagnostic_id\": \"diag-stub-runtime-001\",\n"
        u"  \"doc_snapshot_hash\": \"sha256:0000000000000000000000000000000000000000000000000000000000000000\",\n"
        u"  \"preview_only\": false,\n"
        u"  \"patches\": [\n"
        u"    {\n"
        u"      \"patch_id\": \"p1\",\n"
        u"      \"kind\": \"paragraph-replace\",\n"
        u"      \"target\": {\"paragraph_id\": \"");
    lcl_appendJsonEscaped(aJson, aParagraphId);
    aJson.append(
        u"\"},\n"
        u"      \"severity\": \"minor\",\n"
        u"      \"rationale\": \"offline stub runtime plan\",\n"
        u"      \"after\": \"");
    lcl_appendJsonEscaped(aJson, aAfter);
    aJson.append(u"\"\n    }\n  ]\n}");
    return aJson.makeStringAndClear();
}

} // namespace kqoffice::ai

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */