/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * V2 W2 Day-1b — dispatcher + loader contract tests (pure path, no URE/VCL).
 *
 * BUILDDIR 非 ASCII 时 UnoApiTest/loadFromURL 的 services.rdb 路径会损坏
 * (B2-class)；本 harness 与 kqoffice_provider / cui_commandpalette_fuzzy 一样
 * 走纯 C++ 快路径。集成测试（真实 SfxViewFrame + swriter）留给 ASCII BUILDDIR
 * 或 CI。
 */

#include <sal/types.h>
#include <cppunit/TestAssert.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <commandpalette/CommandIndex.hxx>
#include <dispatch/CommandPaletteDispatcher.hxx>

#include <fstream>
#include <sstream>
#include <string>

namespace
{
using cui::commandpalette::CommandEntry;
using cui::commandpalette::CommandIndex;

OString readFile(const char* path)
{
    std::ifstream in(path);
    CPPUNIT_ASSERT_MESSAGE(path, in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return OString(ss.str().c_str());
}

class CommandPaletteDispatcherTest : public CppUnit::TestFixture
{
public:
    void testLoadCorpusNonEmpty();
    void testLoadCorpusContainsSave();
    void testFrequencyIncrement();
    void testShowPaletteHookRegistered();
    void testDispatchLookupRecursion();
    void testChatFallbackCommandRegistered();

    CPPUNIT_TEST_SUITE(CommandPaletteDispatcherTest);
    CPPUNIT_TEST(testLoadCorpusNonEmpty);
    CPPUNIT_TEST(testLoadCorpusContainsSave);
    CPPUNIT_TEST(testFrequencyIncrement);
    CPPUNIT_TEST(testShowPaletteHookRegistered);
    CPPUNIT_TEST(testDispatchLookupRecursion);
    CPPUNIT_TEST(testChatFallbackCommandRegistered);
    CPPUNIT_TEST_SUITE_END();
};

void CommandPaletteDispatcherTest::testLoadCorpusNonEmpty()
{
    const OString body = readFile(
        SRCDIR "/officecfg/registry/data/org/openoffice/Office/UI/GenericCommands.xcu");
    const std::vector<CommandEntry> corpus = CommandIndex::parseCommandsXcu(body);
    CPPUNIT_ASSERT_GREATER(static_cast<std::size_t>(100), corpus.size());
}

void CommandPaletteDispatcherTest::testLoadCorpusContainsSave()
{
    const OString body = readFile(
        SRCDIR "/officecfg/registry/data/org/openoffice/Office/UI/GenericCommands.xcu");
    const std::vector<CommandEntry> corpus = CommandIndex::parseCommandsXcu(body);
    bool bFound = false;
    for (const auto& e : corpus)
    {
        if (e.unoCommand == u".uno:Save"_ustr)
        {
            bFound = true;
            break;
        }
    }
    CPPUNIT_ASSERT(bFound);
}

void CommandPaletteDispatcherTest::testFrequencyIncrement()
{
    auto& rDisp = sfx2::CommandPaletteDispatcher::Get();
    const OUString aUrl(u".uno:Save"_ustr);
    rDisp.trackCommandUse(aUrl);
    rDisp.trackCommandUse(aUrl);
    rDisp.trackCommandUse(aUrl);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt32>(3), rDisp.frequency(aUrl));
}

void CommandPaletteDispatcherTest::testShowPaletteHookRegistered()
{
    // Registration must not throw; invocation needs a live SfxViewFrame (CI/ASCII BUILDDIR).
    sfx2::CommandPaletteDispatcher::RegisterShowPaletteHook(
        [](SfxViewFrame&) {});
    CPPUNIT_ASSERT(true);
}

void CommandPaletteDispatcherTest::testDispatchLookupRecursion()
{
    auto& rDisp = sfx2::CommandPaletteDispatcher::Get();
    const OUString aSelf(u".uno:CommandPalette"_ustr);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt32>(0), rDisp.frequency(aSelf));
    rDisp.trackCommandUse(aSelf);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt32>(0), rDisp.frequency(aSelf));
}

void CommandPaletteDispatcherTest::testChatFallbackCommandRegistered()
{
    const OString body = readFile(
        SRCDIR "/officecfg/registry/data/org/openoffice/Office/UI/GenericCommands.xcu");
    CPPUNIT_ASSERT(body.indexOf(".uno:SidebarDeck.AIChatDeck") >= 0);
    CPPUNIT_ASSERT(body.indexOf("Open the AI Chat Deck") >= 0);
}

CPPUNIT_TEST_SUITE_REGISTRATION(CommandPaletteDispatcherTest);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
