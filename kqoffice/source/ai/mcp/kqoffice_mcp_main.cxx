/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * kqoffice-mcp — local MCP JSON-RPC host over stdio.
 * Usage:
 *   kqoffice-mcp                 # stdio loop (NDJSON or Content-Length)
 *   kqoffice-mcp --once '<json>' # single request → stdout response
 *   kqoffice-mcp --list          # print tool names
 */

#include <DocumentAIMCPStdio.hxx>
#include <DocumentAIMCPTools.hxx>

#include <rtl/string.hxx>
#include <rtl/ustring.hxx>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

namespace
{
void printHelp()
{
    std::fprintf(stderr,
                 "kqoffice-mcp — 可圈办公 MCP stdio host (local-only)\n"
                 "  kqoffice-mcp                 JSON-RPC over stdin/stdout\n"
                 "  kqoffice-mcp --once '<json>' One-shot request\n"
                 "  kqoffice-mcp --list          List tool names\n"
                 "  kqoffice-mcp --version\n"
                 "Principles: local-first · approve-before-write · no silent upload\n");
}
} // namespace

int main(int argc, char** argv)
{
    using kqoffice::ai::chat::DocumentAIMCPStdio;
    using kqoffice::ai::chat::DocumentAIMCPTools;

    if (argc >= 2)
    {
        const char* a1 = argv[1];
        if (std::strcmp(a1, "-h") == 0 || std::strcmp(a1, "--help") == 0)
        {
            printHelp();
            return 0;
        }
        if (std::strcmp(a1, "--version") == 0 || std::strcmp(a1, "-V") == 0)
        {
            const OString name
                = OUStringToOString(DocumentAIMCPStdio::serverName(), RTL_TEXTENCODING_UTF8);
            const OString ver
                = OUStringToOString(DocumentAIMCPStdio::serverVersion(), RTL_TEXTENCODING_UTF8);
            const OString proto
                = OUStringToOString(DocumentAIMCPStdio::protocolVersion(), RTL_TEXTENCODING_UTF8);
            std::printf("%s %s (protocol %s)\n", name.getStr(), ver.getStr(), proto.getStr());
            return 0;
        }
        if (std::strcmp(a1, "--list") == 0)
        {
            const OString names = OUStringToOString(DocumentAIMCPTools::listToolNamesJson(),
                                                    RTL_TEXTENCODING_UTF8);
            std::printf("%s\n", names.getStr());
            return 0;
        }
        if (std::strcmp(a1, "--once") == 0)
        {
            std::string req;
            if (argc >= 3)
                req = argv[2];
            else
            {
                // read all stdin
                std::string line;
                while (std::getline(std::cin, line))
                {
                    req += line;
                    req.push_back('\n');
                }
            }
            const OUString resp = DocumentAIMCPStdio::handleRequestJson(
                OUString::fromUtf8(std::string_view(req)));
            const OString out = OUStringToOString(resp, RTL_TEXTENCODING_UTF8);
            std::fwrite(out.getStr(), 1, static_cast<size_t>(out.getLength()), stdout);
            std::fputc('\n', stdout);
            return resp.isEmpty() ? 1 : 0;
        }
        printHelp();
        return 2;
    }

    return DocumentAIMCPStdio::runStdioLoop();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
