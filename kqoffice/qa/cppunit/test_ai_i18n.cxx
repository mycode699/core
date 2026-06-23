/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 AI i18n: string provider test).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Unit tests for the header-only AiI18nStrings string provider:
 *   1. zh-CN and en-US return different strings for the same ID.
 *   2. Fallback chain: locale-specific → zh-CN (default) → raw key.
 *   3. All string IDs return non-empty values.
 *   4. format() helper replaces %1 correctly.
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <rtl/ustring.hxx>

#include <cstdlib>
#include <cstring>

#include <AiI18nStrings.hxx>

namespace
{
/// RAII helper to set/unset KQOFFICE_AI_LOCALE for the duration of a test.
class ScopedLocale
{
public:
    explicit ScopedLocale(const char* locale)
    {
        m_hadOld = std::getenv("KQOFFICE_AI_LOCALE") != nullptr;
        if (m_hadOld)
            m_oldValue = std::getenv("KQOFFICE_AI_LOCALE");
        ::setenv("KQOFFICE_AI_LOCALE", locale, 1);
    }
    ~ScopedLocale()
    {
        if (m_hadOld)
            ::setenv("KQOFFICE_AI_LOCALE", m_oldValue.c_str(), 1);
        else
            ::unsetenv("KQOFFICE_AI_LOCALE");
    }

private:
    bool m_hadOld = false;
    std::string m_oldValue;
};

/// All string IDs that must return a non-empty value.
constexpr const char* kAllIds[] = {
    // Provider module
    "provider.error.connection",
    "provider.error.timeout",
    "provider.error.model_not_found",
    "provider.error.no_connection",
    "provider.status.ready",
    "provider.status.connecting",
    "provider.service_mode.local",
    "provider.service_mode.cloud",

    // ApplyPlan validator module
    "applyplan.error.not_json",
    "applyplan.error.missing_field",
    "applyplan.error.schema_mismatch",
    "applyplan.error.id_pattern",
    "applyplan.error.revision_bad",
    "applyplan.error.undo_label_empty",
    "applyplan.error.failure_empty",
    "applyplan.error.summary_empty",

    // ApplyPlan validator — extra granular codes
    "applyplan.error.deterministic",
    "applyplan.error.rollback_required",
    "applyplan.error.undo_group_mode",
    "applyplan.error.failure_behavior_bad",
    "applyplan.error.repeated_diagnostics",
    "applyplan.error.not_json_long",
    "applyplan.error.missing_field_long",
    "applyplan.error.schema_mismatch_long",
    "applyplan.error.id_pattern_long",
    "applyplan.error.revision_bad_long",

    // Cowork module
    "cowork.title",
    "cowork.btn.new_task",
    "cowork.btn.accept_task",
    "cowork.status.this_month",
    "cowork.status.pending",
    "cowork.status.running",
    "cowork.status.awaiting_review",
    "cowork.status.applied",
    "cowork.status.failed",
    "cowork.status.cancelled",
    "cowork.error.enqueue_failed",
    "cowork.error.cancel_failed",
    "cowork.error.no_provider",
    "cowork.notify.task_complete",
    "cowork.notify.task_failed",
    "cowork.notify.awaiting_review",
    "cowork.notify.ready_for_review",
    "cowork.notify.review_ready_body",
    "cowork.task.stub_title",
    "cowork.task.stub_prompt",

    // Formatting helper
    "i18n.field_suffix",

    // About dialog (cui)
    "about.calc_mode.multithreaded",
    "about.calc_mode.jumbo",
    "about.calc_mode.default",
    "about.calc_engine_label",
    "about.ai_section_header",
    "about.ai_features",
};
constexpr sal_Int32 kIdCount = sizeof(kAllIds) / sizeof(kAllIds[0]);

class AiI18nTest : public CppUnit::TestFixture
{
public:
    void testDefaultLocaleIsZhCN();
    void testEnUSReturnsEnglish();
    void testZhCnDiffersFromEnUS();
    void testUnknownIdFallsBackToRawKey();
    void testAllIdsNonEmptyInZhCN();
    void testAllIdsNonEmptyInEnUS();
    void testFormatReplacesPercent1();
    void testFormatNoPercentReturnsTemplate();
    void testLocaleSwitchMidRuntime();

    CPPUNIT_TEST_SUITE(AiI18nTest);
    CPPUNIT_TEST(testDefaultLocaleIsZhCN);
    CPPUNIT_TEST(testEnUSReturnsEnglish);
    CPPUNIT_TEST(testZhCnDiffersFromEnUS);
    CPPUNIT_TEST(testUnknownIdFallsBackToRawKey);
    CPPUNIT_TEST(testAllIdsNonEmptyInZhCN);
    CPPUNIT_TEST(testAllIdsNonEmptyInEnUS);
    CPPUNIT_TEST(testFormatReplacesPercent1);
    CPPUNIT_TEST(testFormatNoPercentReturnsTemplate);
    CPPUNIT_TEST(testLocaleSwitchMidRuntime);
    CPPUNIT_TEST_SUITE_END();
};

void AiI18nTest::testDefaultLocaleIsZhCN()
{
    using kqoffice::ai::i18n::get;
    // No KQOFFICE_AI_LOCALE set → zh-CN.
    ::unsetenv("KQOFFICE_AI_LOCALE");
    OUString zh = get(u"provider.status.ready"_ustr);
    CPPUNIT_ASSERT(!zh.isEmpty());
    // zh-CN string should contain Chinese characters.
    bool hasCJK = false;
    for (sal_Int32 i = 0; i < zh.getLength(); ++i)
    {
        if (zh[i] > 0x2FFF)
        {
            hasCJK = true;
            break;
        }
    }
    CPPUNIT_ASSERT_MESSAGE("default locale should return Chinese", hasCJK);
}

void AiI18nTest::testEnUSReturnsEnglish()
{
    using kqoffice::ai::i18n::get;
    ScopedLocale loc("en-US");
    OUString en = get(u"provider.status.ready"_ustr);
    CPPUNIT_ASSERT_EQUAL(u"AI service ready"_ustr, en);
}

void AiI18nTest::testZhCnDiffersFromEnUS()
{
    using kqoffice::ai::i18n::get;
    const OUString id = u"cowork.title"_ustr;

    ScopedLocale zhLoc("zh-CN");
    OUString zh = get(id);
    CPPUNIT_ASSERT(!zh.isEmpty());

    ScopedLocale enLoc("en-US");
    OUString en = get(id);
    CPPUNIT_ASSERT(!en.isEmpty());

    CPPUNIT_ASSERT_MESSAGE("zh-CN and en-US must differ for same ID", zh != en);
}

void AiI18nTest::testUnknownIdFallsBackToRawKey()
{
    using kqoffice::ai::i18n::get;
    // An ID not present in the table is returned as-is.
    const OUString unknownId = u"some.nonexistent.id"_ustr;
    CPPUNIT_ASSERT_EQUAL(unknownId, get(unknownId));
}

void AiI18nTest::testAllIdsNonEmptyInZhCN()
{
    using kqoffice::ai::i18n::get;
    ::unsetenv("KQOFFICE_AI_LOCALE");
    for (sal_Int32 i = 0; i < kIdCount; ++i)
    {
        OUString idStr = OUString::createFromAscii(kAllIds[i]);
        OUString val = get(idStr);
        CPPUNIT_ASSERT_MESSAGE(
            OString(OString::Concat("zh-CN value for '") + kAllIds[i] + "' must not be empty"),
            !val.isEmpty());
    }
}

void AiI18nTest::testAllIdsNonEmptyInEnUS()
{
    using kqoffice::ai::i18n::get;
    ScopedLocale loc("en-US");
    for (sal_Int32 i = 0; i < kIdCount; ++i)
    {
        OUString idStr = OUString::createFromAscii(kAllIds[i]);
        OUString val = get(idStr);
        CPPUNIT_ASSERT_MESSAGE(
            OString(OString::Concat("en-US value for '") + kAllIds[i] + "' must not be empty"),
            !val.isEmpty());
    }
}

void AiI18nTest::testFormatReplacesPercent1()
{
    using kqoffice::ai::i18n::format;
    ::unsetenv("KQOFFICE_AI_LOCALE");
    OUString result = format(u"cowork.notify.task_complete"_ustr,
                             u"MyTask"_ustr);
    CPPUNIT_ASSERT(!result.isEmpty());
    CPPUNIT_ASSERT(result.indexOf(u"MyTask"_ustr) >= 0);
    // Template "%1" should have been replaced.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(-1),
                         result.indexOf(u"%1"_ustr));
}

void AiI18nTest::testFormatNoPercentReturnsTemplate()
{
    using kqoffice::ai::i18n::format;
    ::unsetenv("KQOFFICE_AI_LOCALE");
    // A string without %1 returns the template unchanged.
    OUString result = format(u"cowork.title"_ustr, u"ignored"_ustr);
    CPPUNIT_ASSERT(!result.isEmpty());
    CPPUNIT_ASSERT_EQUAL(u"异步任务"_ustr, result);
}

void AiI18nTest::testLocaleSwitchMidRuntime()
{
    using kqoffice::ai::i18n::get;
    const OUString id = u"cowork.btn.new_task"_ustr;

    // Start with en-US.
    ::setenv("KQOFFICE_AI_LOCALE", "en-US", 1);
    OUString en = get(id);
    CPPUNIT_ASSERT_EQUAL(u"New Task"_ustr, en);

    // Switch to zh-CN via env var — get() must pick it up (env var is live).
    ::setenv("KQOFFICE_AI_LOCALE", "zh-CN", 1);
    OUString zh = get(id);
    CPPUNIT_ASSERT_EQUAL(u"新建任务"_ustr, zh);

    // Unset falls back to zh-CN (default).
    ::unsetenv("KQOFFICE_AI_LOCALE");
    OUString defVal = get(id);
    // The default (no env var) is zh-CN, so should match.
    CPPUNIT_ASSERT_MESSAGE("default locale should be zh-CN when env var is unset",
                           defVal.indexOf(u"任"_ustr) >= 0);
}

CPPUNIT_TEST_SUITE_REGISTRATION(AiI18nTest);
} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
