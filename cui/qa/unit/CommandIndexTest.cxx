/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W2: Cmd+K Command Palette).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Day-1a unit tests for the pure-logic XCU parser (CommandIndex). No
 * VCL bring-up, no UNO bootstrap, no disk I/O — fixtures are inline
 * XML literals so the binary stays fast (<100ms target).
 */

#include <sal/types.h>
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/plugin/TestPlugIn.h>

#include <commandpalette/CommandIndex.hxx>

using cui::commandpalette::CommandEntry;
using cui::commandpalette::CommandIndex;

namespace
{
class CommandIndexTest : public CppUnit::TestFixture
{
public:
    void testEmptyBufferYieldsEmpty();
    void testSingleSimpleEntry();
    void testStripsAcceleratorTilde();
    void testSkipsEntriesWithoutEnUsLabel();
    void testHandlesMultipleEntriesWithExtraProps();
    void testIgnoresNonUnoNodes();
    void testRobustToTruncatedInput();
    void testFixtureBatchYieldsFourEntries();

    CPPUNIT_TEST_SUITE(CommandIndexTest);
    CPPUNIT_TEST(testEmptyBufferYieldsEmpty);
    CPPUNIT_TEST(testSingleSimpleEntry);
    CPPUNIT_TEST(testStripsAcceleratorTilde);
    CPPUNIT_TEST(testSkipsEntriesWithoutEnUsLabel);
    CPPUNIT_TEST(testHandlesMultipleEntriesWithExtraProps);
    CPPUNIT_TEST(testIgnoresNonUnoNodes);
    CPPUNIT_TEST(testRobustToTruncatedInput);
    CPPUNIT_TEST(testFixtureBatchYieldsFourEntries);
    CPPUNIT_TEST_SUITE_END();
};

void CommandIndexTest::testEmptyBufferYieldsEmpty()
{
    CPPUNIT_ASSERT(CommandIndex::parseCommandsXcu(OString()).empty());
    CPPUNIT_ASSERT(
        CommandIndex::parseCommandsXcu(OString("<no-commands/>")).empty());
}

void CommandIndexTest::testSingleSimpleEntry()
{
    OString body(
        "<oor:component-data>"
        "<node oor:name=\".uno:Bold\" oor:op=\"replace\">"
        "<prop oor:name=\"Label\" oor:type=\"xs:string\">"
        "<value xml:lang=\"en-US\">Bold</value>"
        "</prop></node></oor:component-data>");
    auto entries = CommandIndex::parseCommandsXcu(body);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), entries.size());
    CPPUNIT_ASSERT_EQUAL(u".uno:Bold"_ustr, entries[0].unoCommand);
    CPPUNIT_ASSERT_EQUAL(u"Bold"_ustr, entries[0].labelEn);
    // Day-1a leaves zh/pinyin empty (W2 Day-1c fills them).
    CPPUNIT_ASSERT(entries[0].labelZh.isEmpty());
    CPPUNIT_ASSERT(entries[0].pinyinFirst.isEmpty());
}

void CommandIndexTest::testStripsAcceleratorTilde()
{
    // ShowAnnotations carries a literal `~` accelerator marker before
    // the underlined letter. The parser must drop it so fuzzy matching
    // sees plain "Show Comments".
    OString body(
        "<oor:component-data>"
        "<node oor:name=\".uno:ShowAnnotations\" oor:op=\"replace\">"
        "<prop oor:name=\"Label\" oor:type=\"xs:string\">"
        "<value xml:lang=\"en-US\">Show Comme~nts</value>"
        "</prop></node></oor:component-data>");
    auto entries = CommandIndex::parseCommandsXcu(body);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), entries.size());
    CPPUNIT_ASSERT_EQUAL(u"Show Comments"_ustr, entries[0].labelEn);
}

void CommandIndexTest::testSkipsEntriesWithoutEnUsLabel()
{
    // Only de-DE label present — entry should still emit (we keep the
    // unoCommand for the corpus universe), but labelEn stays empty.
    OString body(
        "<oor:component-data>"
        "<node oor:name=\".uno:LocalOnly\" oor:op=\"replace\">"
        "<prop oor:name=\"Label\" oor:type=\"xs:string\">"
        "<value xml:lang=\"de-DE\">Fett</value>"
        "</prop></node></oor:component-data>");
    auto entries = CommandIndex::parseCommandsXcu(body);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), entries.size());
    CPPUNIT_ASSERT_EQUAL(u".uno:LocalOnly"_ustr, entries[0].unoCommand);
    CPPUNIT_ASSERT(entries[0].labelEn.isEmpty());
}

void CommandIndexTest::testHandlesMultipleEntriesWithExtraProps()
{
    // ContextLabel + Properties props live alongside Label. The parser
    // must lift only the Label en-US value and not be confused by
    // ContextLabel's own xml:lang children.
    OString body(
        "<oor:component-data>"
        "<node oor:name=\".uno:Italic\" oor:op=\"replace\">"
        "<prop oor:name=\"Label\" oor:type=\"xs:string\">"
        "<value xml:lang=\"en-US\">Italic</value>"
        "</prop>"
        "<prop oor:name=\"Properties\" oor:type=\"xs:int\">"
        "<value>1</value></prop>"
        "</node>"
        "<node oor:name=\".uno:Underline\" oor:op=\"replace\">"
        "<prop oor:name=\"ContextLabel\" oor:type=\"xs:string\">"
        "<value xml:lang=\"en-US\">U-line ctx</value></prop>"
        "<prop oor:name=\"Label\" oor:type=\"xs:string\">"
        "<value xml:lang=\"en-US\">Underline</value>"
        "</prop></node>"
        "</oor:component-data>");
    auto entries = CommandIndex::parseCommandsXcu(body);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(2), entries.size());
    CPPUNIT_ASSERT_EQUAL(u".uno:Italic"_ustr, entries[0].unoCommand);
    CPPUNIT_ASSERT_EQUAL(u"Italic"_ustr, entries[0].labelEn);
    CPPUNIT_ASSERT_EQUAL(u".uno:Underline"_ustr, entries[1].unoCommand);
    CPPUNIT_ASSERT_EQUAL(u"Underline"_ustr, entries[1].labelEn);
}

void CommandIndexTest::testIgnoresNonUnoNodes()
{
    // Outer UserInterface / Commands wrapper nodes must NOT be emitted.
    OString body(
        "<oor:component-data>"
        "<node oor:name=\"UserInterface\">"
        "<node oor:name=\"Commands\">"
        "<node oor:name=\".uno:Real\" oor:op=\"replace\">"
        "<prop oor:name=\"Label\" oor:type=\"xs:string\">"
        "<value xml:lang=\"en-US\">Real</value>"
        "</prop></node>"
        "</node></node></oor:component-data>");
    auto entries = CommandIndex::parseCommandsXcu(body);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), entries.size());
    CPPUNIT_ASSERT_EQUAL(u".uno:Real"_ustr, entries[0].unoCommand);
}

void CommandIndexTest::testRobustToTruncatedInput()
{
    // Truncated mid-node: parser must NOT crash and must NOT produce a
    // half-formed entry.
    OString body(
        "<oor:component-data>"
        "<node oor:name=\".uno:Bold\" oor:op=\"replace\">"
        "<prop oor:name=\"Label\" oor:type=\"xs:string\">"
        "<value xml:lang=\"en-US\">Bold</valu");
    auto entries = CommandIndex::parseCommandsXcu(body);
    // Acceptable outcomes: zero entries (we bailed out at truncation) or
    // one entry without a label. The strict guarantee is that we do not
    // crash and we never invent text from beyond the buffer.
    CPPUNIT_ASSERT(entries.size() <= 1);
    if (!entries.empty())
        CPPUNIT_ASSERT(entries[0].labelEn.isEmpty());
}

void CommandIndexTest::testFixtureBatchYieldsFourEntries()
{
    // Sanity check on a small fixture that mirrors the rough shape of
    // GenericCommands.xcu — the production file has thousands of these
    // and we already exercise the smaller building blocks above.
    OString body(
        "<oor:component-data>"
        "<node oor:name=\"UserInterface\">"
        "<node oor:name=\"Commands\">"
        "<node oor:name=\".uno:Bold\" oor:op=\"replace\">"
        "<prop oor:name=\"Label\" oor:type=\"xs:string\">"
        "<value xml:lang=\"en-US\">~Bold</value>"
        "</prop></node>"
        "<node oor:name=\".uno:Italic\" oor:op=\"replace\">"
        "<prop oor:name=\"Label\" oor:type=\"xs:string\">"
        "<value xml:lang=\"en-US\">Italic</value>"
        "</prop></node>"
        "<node oor:name=\".uno:InsertGraphic\" oor:op=\"replace\">"
        "<prop oor:name=\"Label\" oor:type=\"xs:string\">"
        "<value xml:lang=\"en-US\">Insert Image...</value>"
        "</prop></node>"
        "<node oor:name=\".uno:ExportToPDF\" oor:op=\"replace\">"
        "<prop oor:name=\"Label\" oor:type=\"xs:string\">"
        "<value xml:lang=\"en-US\">Export as PDF...</value>"
        "</prop></node>"
        "</node></node></oor:component-data>");
    auto entries = CommandIndex::parseCommandsXcu(body);
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(4), entries.size());
    CPPUNIT_ASSERT_EQUAL(u"Bold"_ustr, entries[0].labelEn);
    CPPUNIT_ASSERT_EQUAL(u"Italic"_ustr, entries[1].labelEn);
    CPPUNIT_ASSERT_EQUAL(u".uno:InsertGraphic"_ustr,
                         entries[2].unoCommand);
    CPPUNIT_ASSERT_EQUAL(u"Insert Image..."_ustr, entries[2].labelEn);
    CPPUNIT_ASSERT_EQUAL(u".uno:ExportToPDF"_ustr, entries[3].unoCommand);
    CPPUNIT_ASSERT_EQUAL(u"Export as PDF..."_ustr, entries[3].labelEn);
}

CPPUNIT_TEST_SUITE_REGISTRATION(CommandIndexTest);
} // namespace

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
