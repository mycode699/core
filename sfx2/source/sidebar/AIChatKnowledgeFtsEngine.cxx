/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V3 M9: real local SQLite FTS5).
 */

#include "AIChatKnowledgeFtsEngine.hxx"

#include "AIChatKnowledgeIndexStore.hxx"

#include <DocumentAIDocumentTools.hxx>
#include <DocumentAIMaterialReader.hxx>

#include <comphelper/hash.hxx>
#include <osl/file.hxx>
#include <osl/time.h>
#include <rtl/ustrbuf.hxx>
#include <sal/log.hxx>
#include <unotools/pathoptions.hxx>

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sfx2::sidebar
{
namespace
{
OUString EnsureNoTrailingSlash(OUString sUrl)
{
    while (sUrl.endsWith(u"/"))
        sUrl = sUrl.copy(0, sUrl.getLength() - 1);
    return sUrl;
}

OUString Sha256Hex(const OUString& rText)
{
    const OString sUtf8 = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
    const std::vector<unsigned char> aHash = comphelper::Hash::calculateHash(
        sUtf8.getStr(), sUtf8.getLength(), comphelper::HashType::SHA256);
    return OUString::createFromAscii(comphelper::hashToString(aHash));
}

OUString Clip(const OUString& s, sal_Int32 nMax)
{
    if (nMax <= 0 || s.getLength() <= nMax)
        return s;
    return s.copy(0, nMax) + u"…"_ustr;
}

bool UrlToSystemPath(const OUString& rUrl, OUString& rPath)
{
    return osl::FileBase::getSystemPathFromFileURL(rUrl, rPath) == osl::FileBase::E_None;
}

/// Escape for FTS5 query: keep alnum/CJK, turn others into spaces, join with AND.
std::string BuildFtsMatchQuery(const OUString& rQueryText)
{
    OUStringBuffer tokens;
    OUStringBuffer cur;
    auto flush = [&]() {
        if (cur.isEmpty())
            return;
        if (!tokens.isEmpty())
            tokens.append(u' ');
        // Quote token for FTS5
        tokens.append(u'"');
        tokens.append(cur.makeStringAndClear());
        tokens.append(u'"');
    };

    for (sal_Int32 i = 0; i < rQueryText.getLength(); ++i)
    {
        const sal_Unicode c = rQueryText[i];
        const bool keep = (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'z')
                          || (c >= u'A' && c <= u'Z') || (c >= 0x4E00 && c <= 0x9FFF)
                          || c == u'_' || c == u'-';
        if (keep)
            cur.append(c);
        else
            flush();
    }
    flush();

    OUString s = tokens.makeStringAndClear();
    if (s.isEmpty())
        return {};
    // FTS5: space-separated quoted tokens are AND by default in many configs;
    // use OR for recall on short Chinese/office queries.
    s = s.replaceAll(u"\" \""_ustr, u"\" OR \""_ustr);
    return std::string(OUStringToOString(s, RTL_TEXTENCODING_UTF8));
}

struct SqliteCloser
{
    void operator()(sqlite3* p) const
    {
        if (p)
            sqlite3_close(p);
    }
};

using SqlitePtr = std::unique_ptr<sqlite3, SqliteCloser>;

SqlitePtr OpenDb(const OUString& rSystemPath, bool bCreate)
{
    sqlite3* pDb = nullptr;
    const int flags = bCreate ? (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) : SQLITE_OPEN_READONLY;
    const OString path = OUStringToOString(rSystemPath, RTL_TEXTENCODING_UTF8);
    if (sqlite3_open_v2(path.getStr(), &pDb, flags, nullptr) != SQLITE_OK)
    {
        if (pDb)
            sqlite3_close(pDb);
        return {};
    }
    sqlite3_busy_timeout(pDb, 2000);
    return SqlitePtr(pDb);
}

bool Exec(sqlite3* pDb, const char* sql)
{
    char* err = nullptr;
    const int rc = sqlite3_exec(pDb, sql, nullptr, nullptr, &err);
    if (err)
    {
        SAL_WARN("sfx.sidebar", "sqlite: " << err);
        sqlite3_free(err);
    }
    return rc == SQLITE_OK;
}

bool EnsureSchema(sqlite3* pDb)
{
    const bool bFts = Exec(pDb,
                           "CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5("
                           "chunk_id UNINDEXED,"
                           "position UNINDEXED,"
                           "source_kind UNINDEXED,"
                           "evidence_id UNINDEXED,"
                           "text_hash UNINDEXED,"
                           "body,"
                           "tokenize = 'unicode61'"
                           ");");
    const bool bMeta = Exec(pDb, "CREATE TABLE IF NOT EXISTS index_meta("
                                 "key TEXT PRIMARY KEY NOT NULL,"
                                 "value TEXT NOT NULL"
                                 ");");
    return bFts && bMeta;
}

OUString ReadMeta(sqlite3* pDb, const char* key)
{
    sqlite3_stmt* pStmt = nullptr;
    if (sqlite3_prepare_v2(pDb, "SELECT value FROM index_meta WHERE key = ?;", -1, &pStmt, nullptr)
        != SQLITE_OK)
        return {};
    sqlite3_bind_text(pStmt, 1, key, -1, SQLITE_STATIC);
    OUString out;
    if (sqlite3_step(pStmt) == SQLITE_ROW)
    {
        const unsigned char* p = sqlite3_column_text(pStmt, 0);
        const int n = sqlite3_column_bytes(pStmt, 0);
        if (p && n > 0)
            out = OUString(reinterpret_cast<const char*>(p), n, RTL_TEXTENCODING_UTF8);
    }
    sqlite3_finalize(pStmt);
    return out;
}

bool WriteMeta(sqlite3* pDb, const char* key, const OUString& rValue)
{
    sqlite3_stmt* pStmt = nullptr;
    if (sqlite3_prepare_v2(pDb,
                           "INSERT INTO index_meta(key,value) VALUES(?,?) "
                           "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
                           -1, &pStmt, nullptr)
        != SQLITE_OK)
        return false;
    const OString val = OUStringToOString(rValue, RTL_TEXTENCODING_UTF8);
    sqlite3_bind_text(pStmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(pStmt, 2, val.getStr(), val.getLength(), SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(pStmt) == SQLITE_DONE;
    sqlite3_finalize(pStmt);
    return ok;
}

AIChatKnowledgeFtsIndexResult IndexOpenDocumentImpl(const OUString& rWorkspaceIdentity,
                                                    bool bForce)
{
    AIChatKnowledgeFtsIndexResult out;
    if (!AIChatKnowledgeFtsEngine::IsSqliteAvailable())
    {
        out.Message = u"fts-index-failed reason=sqlite-unavailable"_ustr;
        return out;
    }

    out.WorkspaceHash = AIChatKnowledgeFtsEngine::MakeWorkspaceHash(rWorkspaceIdentity);
    const OUString dbUrl = AIChatKnowledgeFtsEngine::ResolveDbUrl(out.WorkspaceHash);
    OUString sysPath;
    if (!UrlToSystemPath(dbUrl, sysPath))
    {
        out.Message = u"fts-index-failed reason=path-resolve"_ustr;
        return out;
    }

    auto pDb = OpenDb(sysPath, /*bCreate*/ true);
    if (!pDb)
    {
        out.Message = u"fts-index-failed reason=open-db"_ustr;
        return out;
    }
    if (!EnsureSchema(pDb.get()))
    {
        out.Message = u"fts-index-failed reason=schema"_ustr;
        return out;
    }

    using kqoffice::ai::chat::DocumentAIDocumentTools;
    const auto sk = DocumentAIDocumentTools::buildSkeleton(200, 8000, 80);
    if (!sk.hasDocument || sk.blocks.empty())
    {
        out.Message = u"fts-index-complete count=0 reason=no-open-document incremental=true"_ustr;
        out.Success = true;
        return out;
    }

    // M10: skip rebuild when document structure snapshot is unchanged.
    if (!bForce && !sk.snapshotHash.isEmpty())
    {
        const OUString prev = ReadMeta(pDb.get(), "doc_snapshot");
        if (prev == sk.snapshotHash)
        {
            // Still report approximate row count for UI.
            sal_Int32 nCount = 0;
            sqlite3_stmt* pCount = nullptr;
            if (sqlite3_prepare_v2(pDb.get(), "SELECT count(*) FROM chunks_fts;", -1, &pCount,
                                   nullptr)
                == SQLITE_OK)
            {
                if (sqlite3_step(pCount) == SQLITE_ROW)
                    nCount = sqlite3_column_int(pCount, 0);
                sqlite3_finalize(pCount);
            }
            out.IndexedCount = nCount;
            out.Success = true;
            out.Message = u"fts-index-skipped reason=snapshot-unchanged backend=sqlite-fts5 count="_ustr
                          + OUString::number(nCount) + u" snapshot="_ustr + sk.snapshotHash
                          + u" incremental=true public-egress=false"_ustr;
            return out;
        }
    }

    // Rebuild open-document chunks only; keep external file: rows (M11).
    Exec(pDb.get(), "DELETE FROM chunks_fts WHERE position NOT LIKE 'file:%';");

    sqlite3_stmt* pStmt = nullptr;
    const char* insertSql
        = "INSERT INTO chunks_fts(chunk_id, position, source_kind, evidence_id, text_hash, body) "
          "VALUES(?,?,?,?,?,?);";
    if (sqlite3_prepare_v2(pDb.get(), insertSql, -1, &pStmt, nullptr) != SQLITE_OK)
    {
        out.Message = u"fts-index-failed reason=prepare-insert"_ustr;
        return out;
    }

    AIChatKnowledgeIndexStore aMetaStore(rWorkspaceIdentity.isEmpty()
                                             ? AIChatKnowledgeIndexStore::ResolveCurrentWorkspaceIdentity()
                                             : rWorkspaceIdentity);

    sal_Int32 nIndexed = 0;
    const sal_Int32 nMaxFullReads = std::min<sal_Int32>(sk.blockCount, 80);
    for (sal_Int32 i = 0; i < nMaxFullReads; ++i)
    {
        const auto& blk = sk.blocks[static_cast<size_t>(i)];
        OUString body = blk.preview;
        auto full = DocumentAIDocumentTools::readBlocks(i, i, 0, 4000);
        if (full.success && !full.content.isEmpty())
            body = full.content;
        if (body.isEmpty())
            continue;

        const OUString textHash = Sha256Hex(body);
        const OUString chunkId = AIChatKnowledgeIndexStore::MakeChunkId(
            out.WorkspaceHash, blk.position.isEmpty() ? OUString::number(i) : blk.position, i);
        const OUString evidence = u"evidence:fts:"_ustr + chunkId;

        const OString idUtf8 = OUStringToOString(chunkId, RTL_TEXTENCODING_UTF8);
        const OString posUtf8 = OUStringToOString(blk.position, RTL_TEXTENCODING_UTF8);
        const OString kindUtf8 = OUStringToOString(u"document"_ustr, RTL_TEXTENCODING_UTF8);
        const OString evUtf8 = OUStringToOString(evidence, RTL_TEXTENCODING_UTF8);
        const OString hashUtf8 = OUStringToOString(textHash, RTL_TEXTENCODING_UTF8);
        const OString bodyUtf8 = OUStringToOString(body, RTL_TEXTENCODING_UTF8);

        sqlite3_reset(pStmt);
        sqlite3_clear_bindings(pStmt);
        sqlite3_bind_text(pStmt, 1, idUtf8.getStr(), idUtf8.getLength(), SQLITE_TRANSIENT);
        sqlite3_bind_text(pStmt, 2, posUtf8.getStr(), posUtf8.getLength(), SQLITE_TRANSIENT);
        sqlite3_bind_text(pStmt, 3, kindUtf8.getStr(), kindUtf8.getLength(), SQLITE_TRANSIENT);
        sqlite3_bind_text(pStmt, 4, evUtf8.getStr(), evUtf8.getLength(), SQLITE_TRANSIENT);
        sqlite3_bind_text(pStmt, 5, hashUtf8.getStr(), hashUtf8.getLength(), SQLITE_TRANSIENT);
        sqlite3_bind_text(pStmt, 6, bodyUtf8.getStr(), bodyUtf8.getLength(), SQLITE_TRANSIENT);
        if (sqlite3_step(pStmt) != SQLITE_DONE)
            continue;

        AIChatKnowledgeIndexChunk meta;
        meta.ChunkId = chunkId;
        meta.WorkspaceHash = out.WorkspaceHash;
        meta.SourceKind = u"document"_ustr;
        meta.SourceUriHash = AIChatKnowledgeIndexStore::MakeSourceUriHash(blk.position);
        meta.SourceId = blk.position;
        meta.SnapshotId = sk.snapshotHash;
        meta.ContentHash = textHash;
        meta.TextHash = textHash;
        meta.Granularity = u"paragraph"_ustr;
        meta.Ordinal = i;
        meta.TokenCount = std::clamp<sal_Int32>(std::max<sal_Int32>(1, body.getLength() / 2), 1,
                                                2048);
        meta.Language = u"und"_ustr;
        meta.RetrievalMode = u"fts"_ustr;
        meta.Backend = u"sqlite-fts5"_ustr;
        meta.EvidenceId = evidence;
        meta.HashReference = u"sha256:"_ustr + textHash;
        aMetaStore.RegisterChunkMetadata(meta);

        ++nIndexed;
    }
    sqlite3_finalize(pStmt);

    if (!sk.snapshotHash.isEmpty())
        WriteMeta(pDb.get(), "doc_snapshot", sk.snapshotHash);
    WriteMeta(pDb.get(), "indexed_count", OUString::number(nIndexed));

    out.IndexedCount = nIndexed;
    out.Success = true;
    out.Message = u"fts-index-complete backend=sqlite-fts5 count="_ustr
                  + OUString::number(nIndexed) + u" workspace="_ustr + out.WorkspaceHash
                  + u" snapshot="_ustr + sk.snapshotHash
                  + u" incremental=true forced="_ustr
                  + (bForce ? u"true"_ustr : u"false"_ustr)
                  + u" public-egress=false stores-query-text=false"_ustr;
    return out;
}
}

AIChatKnowledgeFtsIndexResult
AIChatKnowledgeFtsEngine::ForceReindexOpenDocument(const OUString& rWorkspaceIdentity)
{
    return IndexOpenDocumentImpl(rWorkspaceIdentity, /*bForce*/ true);
}

bool AIChatKnowledgeFtsEngine::IsSqliteAvailable()
{
    return sqlite3_libversion_number() >= 3008000;
}

OUString AIChatKnowledgeFtsEngine::MakeWorkspaceHash(const OUString& rWorkspaceIdentity)
{
    const OUString id = rWorkspaceIdentity.isEmpty()
                            ? AIChatKnowledgeIndexStore::ResolveCurrentWorkspaceIdentity()
                            : rWorkspaceIdentity;
    return AIChatKnowledgeIndexStore::MakeWorkspaceHash(id);
}

OUString AIChatKnowledgeFtsEngine::ResolveDbUrl(const OUString& rWorkspaceHash)
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);
    const OUString dir = EnsureNoTrailingSlash(sRoot) + u"/kqoffice-v3-knowledge-fts"_ustr;
    osl::Directory::createPath(dir);
    const OUString hash
        = rWorkspaceHash.isEmpty() ? u"default"_ustr : rWorkspaceHash.copy(0, std::min<sal_Int32>(32, rWorkspaceHash.getLength()));
    return dir + u"/"_ustr + hash + u".sqlite"_ustr;
}

AIChatKnowledgeFtsIndexResult
AIChatKnowledgeFtsEngine::IndexOpenDocument(const OUString& rWorkspaceIdentity)
{
    return IndexOpenDocumentImpl(rWorkspaceIdentity, /*bForce*/ false);
}

AIChatKnowledgeFtsSearchResult
AIChatKnowledgeFtsEngine::Search(const OUString& rQueryText, sal_Int32 nTopK,
                                 const OUString& rWorkspaceIdentity)
{
    AIChatKnowledgeFtsSearchResult out;
    out.Backend = u"sqlite-fts5"_ustr;
    out.WorkspaceHash = MakeWorkspaceHash(rWorkspaceIdentity);

    if (!IsSqliteAvailable())
    {
        out.Message = u"fts-search-failed reason=sqlite-unavailable"_ustr;
        return out;
    }
    if (rQueryText.trim().isEmpty())
    {
        out.Message = u"fts-search-failed reason=empty-query"_ustr;
        return out;
    }

    const sal_Int32 topK = std::clamp(nTopK, sal_Int32(1), sal_Int32(10));
    const auto t0 = std::chrono::steady_clock::now();

    const OUString dbUrl = ResolveDbUrl(out.WorkspaceHash);
    OUString sysPath;
    if (!UrlToSystemPath(dbUrl, sysPath))
    {
        out.Message = u"fts-search-failed reason=path-resolve"_ustr;
        return out;
    }

    // Ensure index exists for current document when missing/empty.
    bool needIndex = true;
    {
        auto pProbe = OpenDb(sysPath, /*bCreate*/ false);
        if (pProbe)
        {
            sqlite3_stmt* pCount = nullptr;
            if (sqlite3_prepare_v2(pProbe.get(), "SELECT count(*) FROM chunks_fts;", -1, &pCount,
                                   nullptr)
                == SQLITE_OK)
            {
                if (sqlite3_step(pCount) == SQLITE_ROW && sqlite3_column_int(pCount, 0) > 0)
                    needIndex = false;
                sqlite3_finalize(pCount);
            }
        }
    }
    if (needIndex)
        IndexOpenDocument(rWorkspaceIdentity);

    auto pDb = OpenDb(sysPath, /*bCreate*/ false);
    if (!pDb)
    {
        out.Message = u"fts-search-failed reason=open-db"_ustr;
        return out;
    }

    const std::string match = BuildFtsMatchQuery(rQueryText);
    if (match.empty())
    {
        out.Message = u"fts-search-failed reason=no-searchable-tokens"_ustr;
        return out;
    }

    const char* sql = "SELECT chunk_id, position, source_kind, evidence_id, text_hash, "
                      "snippet(chunks_fts, 5, '[', ']', '…', 16), bm25(chunks_fts) "
                      "FROM chunks_fts WHERE chunks_fts MATCH ? "
                      "ORDER BY bm25(chunks_fts) LIMIT ?;";
    sqlite3_stmt* pStmt = nullptr;
    if (sqlite3_prepare_v2(pDb.get(), sql, -1, &pStmt, nullptr) != SQLITE_OK)
    {
        out.Message = u"fts-search-failed reason=prepare-search"_ustr;
        return out;
    }
    sqlite3_bind_text(pStmt, 1, match.c_str(), static_cast<int>(match.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(pStmt, 2, topK);

    sal_Int32 rank = 1;
    while (sqlite3_step(pStmt) == SQLITE_ROW)
    {
        AIChatKnowledgeFtsHit hit;
        // Columns may be UTF-8 non-ascii — always decode as UTF-8.
        auto colUtf8 = [&](int i) -> OUString {
            const unsigned char* p = sqlite3_column_text(pStmt, i);
            const int n = sqlite3_column_bytes(pStmt, i);
            if (!p || n <= 0)
                return {};
            return OUString(reinterpret_cast<const char*>(p), n, RTL_TEXTENCODING_UTF8);
        };

        hit.ChunkId = colUtf8(0);
        hit.Position = colUtf8(1);
        hit.SourceKind = colUtf8(2);
        hit.EvidenceId = colUtf8(3);
        hit.TextHash = colUtf8(4);
        hit.Snippet = Clip(colUtf8(5), 400);
        hit.Rank = rank;
        const double bm = sqlite3_column_double(pStmt, 6);
        // bm25 is typically negative (lower is better); map to basis points.
        const sal_Int32 score = static_cast<sal_Int32>(
            std::clamp(10000.0 + bm * -800.0, 0.0, 10000.0));
        hit.ScoreBasisPoints = score;
        out.Hits.push_back(hit);
        ++rank;
    }
    sqlite3_finalize(pStmt);

    const auto t1 = std::chrono::steady_clock::now();
    out.LatencyMs = static_cast<sal_Int32>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
    out.Success = true;
    out.Message = u"fts-search-complete backend=sqlite-fts5 hit-count="_ustr
                  + OUString::number(static_cast<sal_Int32>(out.Hits.size()))
                  + u" top-k="_ustr + OUString::number(topK) + u" latency-ms="_ustr
                  + OUString::number(out.LatencyMs)
                  + u" public-egress=false stores-query-text=false raw-query-not-persisted=true"_ustr;
    return out;
}

OUString AIChatKnowledgeFtsEngine::BuildPromptBlock(const AIChatKnowledgeFtsSearchResult& rSearch,
                                                    sal_Int32 nMaxChars)
{
    if (!rSearch.Success || rSearch.Hits.empty())
        return {};
    OUStringBuffer b;
    b.append(u"【本地 FTS5 检索 · sqlite-fts5 · 仅当前工作区 · 无外传】\n"_ustr);
    for (const auto& h : rSearch.Hits)
    {
        if (b.getLength() >= nMaxChars)
            break;
        b.append(u"["_ustr);
        b.append(h.Rank);
        b.append(u"] "_ustr);
        b.append(h.Position.isEmpty() ? h.ChunkId : h.Position);
        b.append(u" score="_ustr);
        b.append(h.ScoreBasisPoints);
        b.append(u"\n"_ustr);
        b.append(Clip(h.Snippet, 600));
        b.append(u"\n\n"_ustr);
    }
    b.append(u"纪律：以上为本地检索片段；写回仍须 ApplyPlan + 用户批准；主文档未改。\n"_ustr);
    OUString out = b.makeStringAndClear();
    if (out.getLength() > nMaxChars)
        out = out.copy(0, nMaxChars) + u"…"_ustr;
    return out;
}

OUString AIChatKnowledgeFtsEngine::ResolveStorageRootUrl()
{
    const SvtPathOptions aPathOptions;
    OUString sRoot = EnsureNoTrailingSlash(aPathOptions.GetUserConfigPath());
    if (sRoot.isEmpty())
        osl::File::getTempDirURL(sRoot);
    const OUString dir = EnsureNoTrailingSlash(sRoot) + u"/kqoffice-v3-knowledge-fts"_ustr;
    osl::Directory::createPath(dir);
    return dir;
}

namespace
{
sal_Int64 FileMtimeSeconds(const OUString& rSystemPath)
{
    OUString sUrl;
    if (osl::FileBase::getFileURLFromSystemPath(rSystemPath, sUrl) != osl::FileBase::E_None)
        return 0;
    osl::DirectoryItem aItem;
    if (osl::DirectoryItem::get(sUrl, aItem) != osl::FileBase::E_None)
        return 0;
    osl::FileStatus aStatus(osl_FileStatus_Mask_ModifyTime);
    if (aItem.getFileStatus(aStatus) != osl::FileBase::E_None)
        return 0;
    const TimeValue tv = aStatus.getModifyTime();
    return static_cast<sal_Int64>(tv.Seconds);
}

OUString NowIsoLocal()
{
    TimeValue t{};
    osl_getSystemTime(&t);
    // Compact local stamp for status UI (not a crypto timestamp).
    return OUString::number(static_cast<sal_Int64>(t.Seconds));
}

sal_Int32 CountChunks(sqlite3* pDb, const char* whereSql)
{
    sqlite3_stmt* pStmt = nullptr;
    OUString sql = u"SELECT count(*) FROM chunks_fts"_ustr;
    if (whereSql && *whereSql)
        sql += u" WHERE "_ustr + OUString::createFromAscii(whereSql);
    sql += u";"_ustr;
    const OString s = OUStringToOString(sql, RTL_TEXTENCODING_UTF8);
    if (sqlite3_prepare_v2(pDb, s.getStr(), -1, &pStmt, nullptr) != SQLITE_OK)
        return 0;
    sal_Int32 n = 0;
    if (sqlite3_step(pStmt) == SQLITE_ROW)
        n = sqlite3_column_int(pStmt, 0);
    sqlite3_finalize(pStmt);
    return n;
}
} // namespace

bool AIChatKnowledgeFtsEngine::IsExternalPathCurrent(const OUString& rSystemPath,
                                                     const OUString& rWorkspaceIdentity)
{
    if (rSystemPath.isEmpty() || !IsSqliteAvailable())
        return false;
    const OUString wsHash = MakeWorkspaceHash(rWorkspaceIdentity);
    const OUString dbUrl = ResolveDbUrl(wsHash);
    OUString sysPath;
    if (!UrlToSystemPath(dbUrl, sysPath))
        return false;
    auto pDb = OpenDb(sysPath, /*bCreate*/ false);
    if (!pDb)
        return false;
    const OUString pathKey = Sha256Hex(rSystemPath).copy(0, 24);
    const sal_Int64 mtime = FileMtimeSeconds(rSystemPath);
    if (mtime <= 0)
        return false;
    const OUString sMetaKey = u"file-mtime:"_ustr + pathKey;
    const OString metaKey = OUStringToOString(sMetaKey, RTL_TEXTENCODING_UTF8);
    const OUString prev = ReadMeta(pDb.get(), metaKey.getStr());
    return prev == OUString::number(mtime);
}

AIChatKnowledgeFtsIndexResult
AIChatKnowledgeFtsEngine::IndexExternalText(const OUString& rSystemPath, const OUString& rBody,
                                            const OUString& rWorkspaceIdentity)
{
    AIChatKnowledgeFtsIndexResult out;
    if (!IsSqliteAvailable())
    {
        out.Message = u"fts-external-index-failed reason=sqlite-unavailable"_ustr;
        return out;
    }
    if (rSystemPath.isEmpty() || rBody.trim().isEmpty())
    {
        out.Message = u"fts-external-index-skipped reason=empty-path-or-body"_ustr;
        out.Success = true;
        return out;
    }

    out.WorkspaceHash = MakeWorkspaceHash(rWorkspaceIdentity);
    const OUString dbUrl = ResolveDbUrl(out.WorkspaceHash);
    OUString sysPath;
    if (!UrlToSystemPath(dbUrl, sysPath))
    {
        out.Message = u"fts-external-index-failed reason=path-resolve"_ustr;
        return out;
    }

    auto pDb = OpenDb(sysPath, /*bCreate*/ true);
    if (!pDb || !EnsureSchema(pDb.get()))
    {
        out.Message = u"fts-external-index-failed reason=open-or-schema"_ustr;
        return out;
    }

    const OUString pathKey = Sha256Hex(rSystemPath).copy(0, 24);
    const sal_Int64 mtime = FileMtimeSeconds(rSystemPath);
    const OUString sMetaKey = u"file-mtime:"_ustr + pathKey;
    const OUString sBodyKey = u"file-body:"_ustr + pathKey;
    const OString metaKey = OUStringToOString(sMetaKey, RTL_TEXTENCODING_UTF8);
    const OString bodyKey = OUStringToOString(sBodyKey, RTL_TEXTENCODING_UTF8);
    const OUString prevMtime = ReadMeta(pDb.get(), metaKey.getStr());
    const OUString curMtime = OUString::number(mtime);
    const OUString bodyHash = Sha256Hex(rBody);
    const OUString prevBody = ReadMeta(pDb.get(), bodyKey.getStr());

    if (prevMtime == curMtime && prevBody == bodyHash)
    {
        out.Success = true;
        out.IndexedCount = 0;
        out.Message = u"fts-external-index-skipped reason=mtime-unchanged path-hash="_ustr + pathKey
                      + u" incremental=true public-egress=false"_ustr;
        return out;
    }

    const OUString position = u"file:"_ustr + rSystemPath;
    // Replace prior rows for this path.
    {
        sqlite3_stmt* pDel = nullptr;
        if (sqlite3_prepare_v2(pDb.get(), "DELETE FROM chunks_fts WHERE position = ?;", -1, &pDel,
                               nullptr)
            == SQLITE_OK)
        {
            const OString pos = OUStringToOString(position, RTL_TEXTENCODING_UTF8);
            sqlite3_bind_text(pDel, 1, pos.getStr(), pos.getLength(), SQLITE_TRANSIENT);
            sqlite3_step(pDel);
            sqlite3_finalize(pDel);
        }
    }

    const OUString chunkId = AIChatKnowledgeIndexStore::MakeChunkId(out.WorkspaceHash, pathKey, 0);
    const OUString evidence = u"evidence:fts-file:"_ustr + pathKey;
    sqlite3_stmt* pIns = nullptr;
    if (sqlite3_prepare_v2(pDb.get(),
                           "INSERT INTO chunks_fts(chunk_id, position, source_kind, evidence_id, "
                           "text_hash, body) VALUES(?,?,?,?,?,?);",
                           -1, &pIns, nullptr)
        != SQLITE_OK)
    {
        out.Message = u"fts-external-index-failed reason=prepare-insert"_ustr;
        return out;
    }
    const OString idUtf8 = OUStringToOString(chunkId, RTL_TEXTENCODING_UTF8);
    const OString posUtf8 = OUStringToOString(position, RTL_TEXTENCODING_UTF8);
    const OString kindUtf8 = OUStringToOString(u"file"_ustr, RTL_TEXTENCODING_UTF8);
    const OString evUtf8 = OUStringToOString(evidence, RTL_TEXTENCODING_UTF8);
    const OString hashUtf8 = OUStringToOString(bodyHash, RTL_TEXTENCODING_UTF8);
    const OString bodyUtf8 = OUStringToOString(Clip(rBody, 48000), RTL_TEXTENCODING_UTF8);
    sqlite3_bind_text(pIns, 1, idUtf8.getStr(), idUtf8.getLength(), SQLITE_TRANSIENT);
    sqlite3_bind_text(pIns, 2, posUtf8.getStr(), posUtf8.getLength(), SQLITE_TRANSIENT);
    sqlite3_bind_text(pIns, 3, kindUtf8.getStr(), kindUtf8.getLength(), SQLITE_TRANSIENT);
    sqlite3_bind_text(pIns, 4, evUtf8.getStr(), evUtf8.getLength(), SQLITE_TRANSIENT);
    sqlite3_bind_text(pIns, 5, hashUtf8.getStr(), hashUtf8.getLength(), SQLITE_TRANSIENT);
    sqlite3_bind_text(pIns, 6, bodyUtf8.getStr(), bodyUtf8.getLength(), SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(pIns) == SQLITE_DONE;
    sqlite3_finalize(pIns);
    if (!ok)
    {
        out.Message = u"fts-external-index-failed reason=insert"_ustr;
        return out;
    }

    WriteMeta(pDb.get(), metaKey.getStr(), curMtime);
    WriteMeta(pDb.get(), bodyKey.getStr(), bodyHash);
    WriteMeta(pDb.get(), "last_indexed_at", NowIsoLocal());

    AIChatKnowledgeIndexStore aMetaStore(rWorkspaceIdentity.isEmpty()
                                             ? AIChatKnowledgeIndexStore::ResolveCurrentWorkspaceIdentity()
                                             : rWorkspaceIdentity);
    AIChatKnowledgeIndexChunk meta;
    meta.ChunkId = chunkId;
    meta.WorkspaceHash = out.WorkspaceHash;
    meta.SourceKind = u"file"_ustr;
    meta.SourceUriHash = AIChatKnowledgeIndexStore::MakeSourceUriHash(rSystemPath);
    meta.SourceId = pathKey;
    meta.SnapshotId = curMtime;
    meta.ContentHash = bodyHash;
    meta.TextHash = bodyHash;
    meta.Granularity = u"paragraph"_ustr;
    meta.Ordinal = 0;
    meta.TokenCount = std::clamp<sal_Int32>(std::max<sal_Int32>(1, rBody.getLength() / 2), 1, 2048);
    meta.Language = u"und"_ustr;
    meta.RetrievalMode = u"fts"_ustr;
    meta.Backend = u"sqlite-fts5"_ustr;
    meta.EvidenceId = evidence;
    meta.HashReference = u"sha256:"_ustr + bodyHash;
    aMetaStore.RegisterChunkMetadata(meta);

    out.Success = true;
    out.IndexedCount = 1;
    out.Message = u"fts-external-index-complete backend=sqlite-fts5 path-hash="_ustr + pathKey
                  + u" mtime="_ustr + curMtime
                  + u" public-egress=false stores-query-text=false incremental=true"_ustr;
    return out;
}

AIChatKnowledgeFtsIndexResult
AIChatKnowledgeFtsEngine::IndexMaterialsFromPrompt(const OUString& rPrompt,
                                                   const OUString& rWorkspaceIdentity)
{
    AIChatKnowledgeFtsIndexResult out;
    out.WorkspaceHash = MakeWorkspaceHash(rWorkspaceIdentity);
    out.Success = true;
    if (rPrompt.isEmpty())
    {
        out.Message = u"fts-materials-index-skipped reason=empty-prompt"_ustr;
        return out;
    }

    using kqoffice::ai::chat::DocumentAIMaterialReader;
    const std::vector<OUString> paths = DocumentAIMaterialReader::collectMentionPaths(rPrompt);
    if (paths.empty())
    {
        out.Message = u"fts-materials-index-skipped reason=no-material-mentions"_ustr;
        return out;
    }

    sal_Int32 nOk = 0;
    sal_Int32 nSeen = 0;
    constexpr sal_Int32 kMaxFiles = 12;
    for (const auto& path : paths)
    {
        if (nSeen >= kMaxFiles)
            break;
        ++nSeen;
        const auto extracted = DocumentAIMaterialReader::extractPath(path, 16000);
        if (!extracted.success || extracted.text.isEmpty())
            continue;
        const auto one = IndexExternalText(path, extracted.text, rWorkspaceIdentity);
        if (one.Success && one.IndexedCount > 0)
            nOk += one.IndexedCount;
        else if (one.Success)
            ; // skipped mtime-unchanged still counts as success
        else
            out.Success = false;
    }
    out.IndexedCount = nOk;
    out.Message = u"fts-materials-index-complete paths="_ustr + OUString::number(nSeen)
                  + u" newly-indexed="_ustr + OUString::number(nOk)
                  + u" public-egress=false local-only=true"_ustr;
    // Auto-register material paths into bounded poll watch (M12; no held FDs).
    RegisterWatchFromPrompt(rPrompt, rWorkspaceIdentity);
    return out;
}

AIChatKnowledgeFtsWorkspaceInfo
AIChatKnowledgeFtsEngine::DescribeWorkspace(const OUString& rWorkspaceIdentity)
{
    AIChatKnowledgeFtsWorkspaceInfo info;
    info.WorkspaceHash = MakeWorkspaceHash(rWorkspaceIdentity);
    info.DbUrl = ResolveDbUrl(info.WorkspaceHash);
    OUString sysPath;
    if (!UrlToSystemPath(info.DbUrl, sysPath))
        return info;
    auto pDb = OpenDb(sysPath, /*bCreate*/ false);
    if (!pDb)
        return info;
    info.Exists = true;
    if (!EnsureSchema(pDb.get()))
        return info;
    info.DocSnapshot = ReadMeta(pDb.get(), "doc_snapshot");
    info.LastIndexedAt = ReadMeta(pDb.get(), "last_indexed_at");
    info.ChunkCount = CountChunks(pDb.get(), nullptr);
    info.ExternalFileCount = CountChunks(pDb.get(), "position LIKE 'file:%'");
    return info;
}

std::vector<AIChatKnowledgeFtsWorkspaceInfo> AIChatKnowledgeFtsEngine::ListWorkspaces()
{
    std::vector<AIChatKnowledgeFtsWorkspaceInfo> out;
    const OUString root = ResolveStorageRootUrl();
    osl::Directory aDir(root);
    if (aDir.open() != osl::FileBase::E_None)
        return out;

    osl::DirectoryItem aItem;
    while (aDir.getNextItem(aItem) == osl::FileBase::E_None)
    {
        osl::FileStatus aStatus(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileName
                                | osl_FileStatus_Mask_FileURL);
        if (aItem.getFileStatus(aStatus) != osl::FileBase::E_None)
            continue;
        if (!aStatus.isRegular())
            continue;
        const OUString name = aStatus.getFileName();
        if (!name.endsWith(u".sqlite"_ustr))
            continue;
        const OUString hash = name.copy(0, name.getLength() - 7);
        AIChatKnowledgeFtsWorkspaceInfo info;
        info.WorkspaceHash = hash;
        info.DbUrl = aStatus.getFileURL();
        OUString sysPath;
        if (UrlToSystemPath(info.DbUrl, sysPath))
        {
            auto pDb = OpenDb(sysPath, false);
            if (pDb && EnsureSchema(pDb.get()))
            {
                info.Exists = true;
                info.DocSnapshot = ReadMeta(pDb.get(), "doc_snapshot");
                info.LastIndexedAt = ReadMeta(pDb.get(), "last_indexed_at");
                info.ChunkCount = CountChunks(pDb.get(), nullptr);
                info.ExternalFileCount = CountChunks(pDb.get(), "position LIKE 'file:%'");
            }
        }
        out.push_back(info);
    }
    aDir.close();
    std::sort(out.begin(), out.end(),
              [](const AIChatKnowledgeFtsWorkspaceInfo& a, const AIChatKnowledgeFtsWorkspaceInfo& b) {
                  return a.WorkspaceHash < b.WorkspaceHash;
              });
    return out;
}

OUString AIChatKnowledgeFtsEngine::FormatWorkspaceStatusZh(const OUString& rWorkspaceIdentity)
{
    const auto info = DescribeWorkspace(rWorkspaceIdentity);
    OUStringBuffer b;
    b.append(u"【本地知识索引状态 · sqlite-fts5 · 无外传】\n"_ustr);
    b.append(u"工作区："_ustr);
    b.append(info.WorkspaceHash.isEmpty() ? u"(unknown)"_ustr : info.WorkspaceHash);
    b.append(u"\n数据库："_ustr);
    b.append(info.Exists ? u"已创建"_ustr : u"尚未创建"_ustr);
    b.append(u"\n块数："_ustr);
    b.append(info.ChunkCount);
    b.append(u"（外部文件块 "_ustr);
    b.append(info.ExternalFileCount);
    b.append(u"）\n文档快照："_ustr);
    b.append(info.DocSnapshot.isEmpty() ? u"(无)"_ustr : info.DocSnapshot);
    b.append(u"\n最近索引："_ustr);
    b.append(info.LastIndexedAt.isEmpty() ? u"(无)"_ustr : info.LastIndexedAt);
    b.append(u"\n命令：/fts-status · /fts-reindex · /fts-workspaces · /fts-index-materials\n"_ustr);
    b.append(u"       /fts-purge · /fts-watch · /fts-watch-status · /fts-watch-poll\n"_ustr);
    b.append(u"主文档不会因索引而改写。\n"_ustr);
    return b.makeStringAndClear();
}

OUString AIChatKnowledgeFtsEngine::FormatWorkspaceListZh()
{
    const auto list = ListWorkspaces();
    OUStringBuffer b;
    b.append(u"【本地 FTS 工作区列表 · 无外传】\n"_ustr);
    if (list.empty())
    {
        b.append(u"（尚无索引库；打开文档并提问或使用 /fts-reindex 创建）\n"_ustr);
        return b.makeStringAndClear();
    }
    sal_Int32 i = 1;
    for (const auto& w : list)
    {
        b.append(OUString::number(i++));
        b.append(u". "_ustr);
        b.append(w.WorkspaceHash);
        b.append(u" · chunks="_ustr);
        b.append(w.ChunkCount);
        b.append(u" · files="_ustr);
        b.append(w.ExternalFileCount);
        b.append(u" · snapshot="_ustr);
        b.append(w.DocSnapshot.isEmpty() ? u"-"_ustr : w.DocSnapshot.copy(0, std::min<sal_Int32>(8, w.DocSnapshot.getLength())));
        b.append(u"\n"_ustr);
    }
    b.append(u"当前工作区："_ustr);
    b.append(MakeWorkspaceHash(OUString()));
    b.append(u"\n"_ustr);
    b.append(u"清理：/fts-purge [hash前缀]（仅删本地索引库，不改主文档）\n"_ustr);
    return b.makeStringAndClear();
}

namespace
{
// M12 bounded material-path poll watch: process-local, no held FDs, cap 256.
// Aligns with w3-watcher-scalability-policy (debounce 5s, maxOpenFileDescriptors 256,
// perFileDescriptorWatch=false, overflow=fail-closed-user-visible).
// This is NOT an OS FSEvents/inotify daemon (runtimeWatcherImplementation remains not-started).
struct WatchEntry
{
    OUString SystemPath;
    sal_Int64 LastMtime = 0;
};

constexpr sal_Int32 kMaxWatchPaths = 256;
constexpr sal_Int32 kDebounceMs = 5000;
constexpr sal_Int32 kPollIntervalSeconds = 60;
constexpr sal_Int32 kMaxReindexPerPoll = 8;

struct WatchState
{
    std::mutex Mutex;
    std::vector<WatchEntry> Paths;
    bool Overflow = false;
    sal_Int64 LastPollSteadyMs = 0;
    OUString LastPollMessage;
};

WatchState& GetWatchState()
{
    static WatchState s;
    return s;
}

sal_Int64 SteadyNowMs()
{
    return static_cast<sal_Int64>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

bool LooksLikeWorkspaceHash(const OUString& s)
{
    if (s.getLength() < 8 || s.getLength() > 64)
        return false;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        const bool hex = (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')
                         || (c >= u'A' && c <= u'F');
        if (!hex)
            return false;
    }
    return true;
}

OUString ResolvePurgeWorkspaceHash(const OUString& rArg)
{
    if (rArg.isEmpty())
        return AIChatKnowledgeFtsEngine::MakeWorkspaceHash(OUString());
    if (LooksLikeWorkspaceHash(rArg))
    {
        // Match list entry by prefix or exact file stem.
        const auto list = AIChatKnowledgeFtsEngine::ListWorkspaces();
        for (const auto& w : list)
        {
            if (w.WorkspaceHash == rArg || w.WorkspaceHash.startsWith(rArg))
                return w.WorkspaceHash;
        }
        // Allow deleting a hash that is not yet listed (path still resolved).
        return rArg.copy(0, std::min<sal_Int32>(32, rArg.getLength()));
    }
    return AIChatKnowledgeFtsEngine::MakeWorkspaceHash(rArg);
}

bool RemoveUrlIfExists(const OUString& rUrl)
{
    // remove returns E_None or E_NOENT-equivalent; treat missing as ok.
    const auto rc = osl::File::remove(rUrl);
    return rc == osl::FileBase::E_None || rc == osl::FileBase::E_NOENT;
}
} // namespace

AIChatKnowledgeFtsPurgeResult
AIChatKnowledgeFtsEngine::PurgeWorkspace(const OUString& rWorkspaceHashOrIdentity)
{
    AIChatKnowledgeFtsPurgeResult out;
    out.WorkspaceHash = ResolvePurgeWorkspaceHash(rWorkspaceHashOrIdentity.trim());
    out.DbUrl = ResolveDbUrl(out.WorkspaceHash);
    if (out.WorkspaceHash.isEmpty())
    {
        out.Message = u"fts-purge-failed reason=empty-workspace public-egress=false"_ustr;
        return out;
    }

    OUString sysPath;
    if (!UrlToSystemPath(out.DbUrl, sysPath))
    {
        out.Message = u"fts-purge-failed reason=path-resolve public-egress=false"_ustr;
        return out;
    }

    // Probe existence before remove for user-visible Removed flag.
    bool existed = false;
    {
        auto pDb = OpenDb(sysPath, /*bCreate*/ false);
        existed = static_cast<bool>(pDb);
        pDb.reset();
    }

    // Drop main db + possible WAL/SHM sidecars (no open connection held).
    const bool okMain = RemoveUrlIfExists(out.DbUrl);
    RemoveUrlIfExists(out.DbUrl + u"-wal"_ustr);
    RemoveUrlIfExists(out.DbUrl + u"-shm"_ustr);
    RemoveUrlIfExists(out.DbUrl + u"-journal"_ustr);

    if (!okMain)
    {
        out.Message = u"fts-purge-failed reason=remove-failed workspace="_ustr + out.WorkspaceHash
                      + u" public-egress=false main-document-unchanged=true"_ustr;
        return out;
    }

    out.Success = true;
    out.Removed = existed;
    out.Message = u"fts-purge-complete workspace="_ustr + out.WorkspaceHash
                  + u" removed="_ustr + (existed ? u"true"_ustr : u"false"_ustr)
                  + u" public-egress=false main-document-unchanged=true "
                    "stores-query-text=false"_ustr;
    return out;
}

OUString AIChatKnowledgeFtsEngine::FormatPurgeResultZh(const AIChatKnowledgeFtsPurgeResult& rResult)
{
    OUStringBuffer b;
    b.append(u"【本地 FTS 工作区清理 · 无外传 · 主文档未改】\n"_ustr);
    b.append(u"工作区："_ustr);
    b.append(rResult.WorkspaceHash.isEmpty() ? u"(unknown)"_ustr : rResult.WorkspaceHash);
    b.append(u"\n结果："_ustr);
    if (!rResult.Success)
        b.append(u"失败"_ustr);
    else if (rResult.Removed)
        b.append(u"已删除索引库"_ustr);
    else
        b.append(u"无需删除（索引库不存在）"_ustr);
    b.append(u"\n"_ustr);
    b.append(rResult.Message);
    b.append(u"\n"_ustr);
    return b.makeStringAndClear();
}

AIChatKnowledgeFtsIndexResult
AIChatKnowledgeFtsEngine::RegisterWatchPaths(const std::vector<OUString>& rSystemPaths,
                                             const OUString& /*rWorkspaceIdentity*/)
{
    AIChatKnowledgeFtsIndexResult out;
    out.Success = true;
    auto& st = GetWatchState();
    std::lock_guard<std::mutex> lock(st.Mutex);

    sal_Int32 nAdded = 0;
    sal_Int32 nDup = 0;
    sal_Int32 nRejected = 0;
    for (const auto& path : rSystemPaths)
    {
        const OUString p = path.trim();
        if (p.isEmpty())
            continue;
        bool exists = false;
        for (const auto& e : st.Paths)
        {
            if (e.SystemPath == p)
            {
                exists = true;
                break;
            }
        }
        if (exists)
        {
            ++nDup;
            continue;
        }
        if (static_cast<sal_Int32>(st.Paths.size()) >= kMaxWatchPaths)
        {
            st.Overflow = true;
            ++nRejected;
            continue;
        }
        WatchEntry e;
        e.SystemPath = p;
        e.LastMtime = FileMtimeSeconds(p);
        st.Paths.push_back(e);
        ++nAdded;
    }

    out.IndexedCount = nAdded;
    if (nRejected > 0)
    {
        // fail-closed-user-visible: do not silently drop beyond cap.
        out.Success = false;
        out.Message = u"fts-watch-register-overflow tracked="_ustr
                      + OUString::number(static_cast<sal_Int32>(st.Paths.size()))
                      + u" max="_ustr + OUString::number(kMaxWatchPaths)
                      + u" added="_ustr + OUString::number(nAdded)
                      + u" rejected="_ustr + OUString::number(nRejected)
                      + u" overflow=true per-file-fd=false strategy=bounded-poll-no-per-file-fd "
                        "debounce-ms="_ustr
                      + OUString::number(kDebounceMs)
                      + u" public-egress=false fail-closed-user-visible=true"_ustr;
    }
    else
    {
        out.Message = u"fts-watch-register-complete tracked="_ustr
                      + OUString::number(static_cast<sal_Int32>(st.Paths.size()))
                      + u" max="_ustr + OUString::number(kMaxWatchPaths)
                      + u" added="_ustr + OUString::number(nAdded)
                      + u" dup="_ustr + OUString::number(nDup)
                      + u" overflow=false per-file-fd=false strategy=bounded-poll-no-per-file-fd "
                        "debounce-ms="_ustr
                      + OUString::number(kDebounceMs)
                      + u" public-egress=false"_ustr;
    }
    return out;
}

AIChatKnowledgeFtsIndexResult
AIChatKnowledgeFtsEngine::RegisterWatchFromPrompt(const OUString& rPrompt,
                                                  const OUString& rWorkspaceIdentity)
{
    using kqoffice::ai::chat::DocumentAIMaterialReader;
    const std::vector<OUString> paths = DocumentAIMaterialReader::collectMentionPaths(rPrompt);
    return RegisterWatchPaths(paths, rWorkspaceIdentity);
}

AIChatKnowledgeFtsIndexResult
AIChatKnowledgeFtsEngine::PollWatchedPaths(const OUString& rWorkspaceIdentity, bool bForce)
{
    AIChatKnowledgeFtsIndexResult out;
    out.WorkspaceHash = MakeWorkspaceHash(rWorkspaceIdentity);
    out.Success = true;

    auto& st = GetWatchState();
    const sal_Int64 now = SteadyNowMs();

    std::vector<OUString> toReindex;
    {
        std::lock_guard<std::mutex> lock(st.Mutex);
        if (!bForce && st.LastPollSteadyMs > 0
            && (now - st.LastPollSteadyMs) < static_cast<sal_Int64>(kDebounceMs))
        {
            out.Message = u"fts-watch-poll-skipped reason=debounce tracked="_ustr
                          + OUString::number(static_cast<sal_Int32>(st.Paths.size()))
                          + u" debounce-ms="_ustr + OUString::number(kDebounceMs)
                          + u" per-file-fd=false public-egress=false"_ustr;
            st.LastPollMessage = out.Message;
            return out;
        }
        st.LastPollSteadyMs = now;

        sal_Int32 nChanged = 0;
        for (auto& e : st.Paths)
        {
            if (nChanged >= kMaxReindexPerPoll)
                break;
            const sal_Int64 m = FileMtimeSeconds(e.SystemPath);
            if (m != 0 && m != e.LastMtime)
            {
                toReindex.push_back(e.SystemPath);
                e.LastMtime = m;
                ++nChanged;
            }
        }
    }

    sal_Int32 nOk = 0;
    for (const auto& path : toReindex)
    {
        const auto extracted
            = kqoffice::ai::chat::DocumentAIMaterialReader::extractPath(path, 16000);
        if (!extracted.success || extracted.text.isEmpty())
            continue;
        const auto one = IndexExternalText(path, extracted.text, rWorkspaceIdentity);
        if (one.Success)
            ++nOk;
        else
            out.Success = false;
    }

    out.IndexedCount = nOk;
    {
        std::lock_guard<std::mutex> lock(st.Mutex);
        out.Message = u"fts-watch-poll-complete tracked="_ustr
                      + OUString::number(static_cast<sal_Int32>(st.Paths.size()))
                      + u" changed="_ustr + OUString::number(static_cast<sal_Int32>(toReindex.size()))
                      + u" reindexed="_ustr + OUString::number(nOk)
                      + u" max="_ustr + OUString::number(kMaxWatchPaths)
                      + u" debounce-ms="_ustr + OUString::number(kDebounceMs)
                      + u" poll-interval-s="_ustr + OUString::number(kPollIntervalSeconds)
                      + u" per-file-fd=false strategy=bounded-poll-no-per-file-fd overflow="_ustr
                      + (st.Overflow ? u"true"_ustr : u"false"_ustr)
                      + u" public-egress=false main-document-unchanged=true"_ustr;
        st.LastPollMessage = out.Message;
    }
    return out;
}

AIChatKnowledgeFtsIndexResult AIChatKnowledgeFtsEngine::ClearWatchList()
{
    AIChatKnowledgeFtsIndexResult out;
    out.Success = true;
    auto& st = GetWatchState();
    std::lock_guard<std::mutex> lock(st.Mutex);
    const sal_Int32 n = static_cast<sal_Int32>(st.Paths.size());
    st.Paths.clear();
    st.Overflow = false;
    st.LastPollSteadyMs = 0;
    st.LastPollMessage.clear();
    out.IndexedCount = 0;
    out.Message = u"fts-watch-clear-complete previous-tracked="_ustr + OUString::number(n)
                  + u" per-file-fd=false public-egress=false"_ustr;
    return out;
}

AIChatKnowledgeFtsWatchStatus AIChatKnowledgeFtsEngine::DescribeWatchStatus()
{
    AIChatKnowledgeFtsWatchStatus s;
    s.MaxPaths = kMaxWatchPaths;
    s.DebounceMs = kDebounceMs;
    s.PollIntervalSeconds = kPollIntervalSeconds;
    s.PerFileDescriptorWatch = false;
    s.Strategy = u"bounded-poll-no-per-file-fd"_ustr;
    auto& st = GetWatchState();
    std::lock_guard<std::mutex> lock(st.Mutex);
    s.TrackedPathCount = static_cast<sal_Int32>(st.Paths.size());
    s.Overflow = st.Overflow;
    s.LastPollMessage = st.LastPollMessage;
    s.Message = u"fts-watch-status tracked="_ustr + OUString::number(s.TrackedPathCount)
                + u" max="_ustr + OUString::number(s.MaxPaths) + u" debounce-ms="_ustr
                + OUString::number(s.DebounceMs) + u" poll-interval-s="_ustr
                + OUString::number(s.PollIntervalSeconds)
                + u" per-file-fd=false strategy=bounded-poll-no-per-file-fd overflow="_ustr
                + (s.Overflow ? u"true"_ustr : u"false"_ustr)
                + u" os-fsevents-daemon=not-started public-egress=false"_ustr;
    return s;
}

OUString AIChatKnowledgeFtsEngine::FormatWatchStatusZh()
{
    const auto s = DescribeWatchStatus();
    OUStringBuffer b;
    b.append(u"【本地材料路径监视 · 有界轮询 · 无外传】\n"_ustr);
    b.append(u"策略："_ustr);
    b.append(s.Strategy);
    b.append(u"\n已跟踪路径："_ustr);
    b.append(s.TrackedPathCount);
    b.append(u" / "_ustr);
    b.append(s.MaxPaths);
    b.append(u"\n防抖："_ustr);
    b.append(s.DebounceMs);
    b.append(u" ms · 轮询间隔："_ustr);
    b.append(s.PollIntervalSeconds);
    b.append(u" s\n每文件 FD："_ustr);
    b.append(s.PerFileDescriptorWatch ? u"是（禁止）"_ustr : u"否（合规）"_ustr);
    b.append(u"\n溢出："_ustr);
    b.append(s.Overflow ? u"是 · 已 fail-closed 对用户可见"_ustr : u"否"_ustr);
    b.append(u"\nOS FSEvents/inotify 守护进程：未启动（contract not-started）\n"_ustr);
    if (!s.LastPollMessage.isEmpty())
    {
        b.append(u"最近轮询："_ustr);
        b.append(s.LastPollMessage);
        b.append(u"\n"_ustr);
    }
    b.append(u"命令：/fts-watch [路径或@材料] · /fts-watch-status · /fts-watch-poll · /fts-watch-clear\n"_ustr);
    b.append(u"主文档不会因监视/轮询而改写。\n"_ustr);
    b.append(s.Message);
    b.append(u"\n"_ustr);
    return b.makeStringAndClear();
}

} // namespace sfx2::sidebar

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
