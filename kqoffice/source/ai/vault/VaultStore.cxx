/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "VaultStore.hxx"
#include "VaultManager.hxx"

#include <AiPaths.hxx>

#include <osl/file.hxx>
#include <rtl/ustrbuf.hxx>

#include <cstdlib>
#include <string>

namespace kqoffice::ai::vault
{
namespace
{
OUString homeConfigBase()
{
    return kqoffice::ai::kqofficeAiConfigDir();
}

bool ensureDir(const OUString& rSysPath)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    const auto rc = osl::Directory::createPath(url);
    return rc == osl::FileBase::E_None || rc == osl::FileBase::E_EXIST;
}

bool writeIfMissing(const OUString& rSysPath, const OUString& rUtf8Body)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    osl::File probe(url);
    if (probe.open(osl_File_OpenFlag_Read) == osl::FileBase::E_None)
    {
        probe.close();
        return true; // already exists
    }
    if (!ensureDir(kqoffice::ai::kqofficeParentDir(rSysPath)))
        return false;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    const OString body = OUStringToOString(rUtf8Body, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(body.getStr(), body.getLength(), n);
    f.close();
    return true;
}

OUString vaultMdTemplate()
{
    // Internal rules for future compile worker. Never show full text in product UI.
    return u"# VAULT (internal)\n"
           u"\n"
           u"User-facing product name: 资料盘. Do not expose RAG/vector/compile terms in UI.\n"
           u"\n"
           u"## Layers\n"
           u"- raw/: immutable source of truth. Append only. Never rewrite.\n"
           u"- wiki/: system-maintained summaries, concepts, index.md, log.md.\n"
           u"- outputs/: qa/, health/, packs/ (资料包).\n"
           u"\n"
           u"## Rules\n"
           u"1. Never modify files under raw/.\n"
           u"2. Every wiki claim must cite a raw source path (or office-link).\n"
           u"3. Conflicts → mark 待核验; do not silently overwrite.\n"
           u"4. Update wiki/index.md and append wiki/log.md on each ingest/compile.\n"
           u"5. Uncertain facts must not be written as certainties.\n"
           u"6. Never mutate the main office document from vault compile.\n"
           u"7. Default retrieval is local FTS; hybrid/vector requires explicit user confirm.\n"
           u"8. Do not store API keys or session tokens in vault files.\n"
           u"\n"
           u"## Defaults\n"
           u"- autoCompileDefault: false\n"
           u"- ftsIndexOnIngestDefault: true\n"
           u"- officeLargeFileDefault: link-not-copy\n"_ustr;
}

bool dirExists(const OUString& rSysPath)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSysPath, url) != osl::FileBase::E_None)
        return false;
    osl::DirectoryItem item;
    if (osl::DirectoryItem::get(url, item) != osl::FileBase::E_None)
        return false;
    osl::FileStatus st(osl_FileStatus_Mask_Type);
    if (item.getFileStatus(st) != osl::FileBase::E_None)
        return false;
    return st.getFileType() == osl::FileStatus::Directory;
}
} // namespace

OUString VaultStore::defaultRootDir()
{
    return homeConfigBase() + u"/vault"_ustr;
}

OUString VaultStore::rootDir()
{
    // Env wins (tests / enterprise override). Else active vault from registry
    // (install default or user-created; Thunder/Quark/WPS-style path management).
    if (const char* env = std::getenv("KQOFFICE_VAULT_ROOT"); env && *env)
        return OUString::fromUtf8(env);
    const OUString active = VaultManager::activeRoot();
    if (!active.isEmpty())
        return active;
    return defaultRootDir();
}

VaultPaths VaultStore::pathsFor(const OUString& rRoot)
{
    VaultPaths p;
    p.root = rRoot.isEmpty() ? rootDir() : rRoot;
    // strip trailing slash (posix + win)
    while (p.root.getLength() > 1 && (p.root.endsWith(u"/") || p.root.endsWith(u"\\")))
        p.root = p.root.copy(0, p.root.getLength() - 1);
    p.raw = p.root + u"/raw"_ustr;
    p.wiki = p.root + u"/wiki"_ustr;
    p.outputs = p.root + u"/outputs"_ustr;
    p.internalState = p.root + u"/.kq"_ustr;
    p.vaultMd = p.root + u"/VAULT.md"_ustr;
    p.stateJson = p.internalState + u"/state.json"_ustr;
    return p;
}

bool VaultStore::ensureLayout(const OUString& rRoot)
{
    const VaultPaths p = pathsFor(rRoot);
    const OUString dirs[] = {
        p.root,
        p.raw,
        p.raw + u"/imports"_ustr,
        p.raw + u"/office-links"_ustr,
        p.raw + u"/notebook"_ustr,
        p.wiki,
        p.wiki + u"/sources"_ustr,
        p.wiki + u"/concepts"_ustr,
        p.wiki + u"/entities"_ustr,
        p.wiki + u"/syntheses"_ustr,
        p.outputs,
        p.outputs + u"/qa"_ustr,
        p.outputs + u"/health"_ustr,
        p.outputs + u"/packs"_ustr,
        p.internalState,
    };
    for (const auto& d : dirs)
    {
        if (!ensureDir(d))
            return false;
    }
    if (!writeIfMissing(p.vaultMd, vaultMdTemplate()))
        return false;
    // User-facing mental model (not internal VAULT.md). Quiet product voice.
    if (!writeIfMissing(
            p.root + u"/开始使用资料盘.md"_ustr,
            u"# 可圈资料盘\n\n"
            u"> **你的材料在变聪明。你仍在用办公软件。**\n\n"
            u"资料盘是可圈办公的本地知识底座：收入过的合同、纪要、表格说明、剪藏与笔记，"
            u"会在本机整理成**可搜索、可引用**的工作资产。\n\n"
            u"## 三步上手\n\n"
            u"1. 在可圈 AI 侧栏输入 `/收入资料 /你的文件或文件夹路径`\n"
            u"2. 用 `/搜资料 关键词` 找回内容\n"
            u"3. 写文档时看侧栏「相关资料」；改主文档仍须**批准写回**\n\n"
            u"## 管理（像下载路径一样简单）\n\n"
            u"- `/资料盘管理` · `/打开资料盘` · `/授权资料盘`\n"
            u"- `/新建资料盘 名称 | /路径` · `/资料盘位置 /新路径`\n\n"
            u"## 我们承诺\n\n"
            u"- **本地优先**：材料与索引默认在本机\n"
            u"- **按目录授权**：只访问你选定的资料盘路径，非整盘\n"
            u"- **写回前确认**：AI 不静默改你的正文\n"
            u"- **不堆术语**：你无需配置「向量库」才能用\n\n"
            u"这就是可圈办公对「未来办公」的定义：科技在底层，心智在桌面。\n"_ustr))
        return false;
    if (!writeIfMissing(p.wiki + u"/index.md"_ustr,
                        u"# 资料盘目录\n\n（系统整理后更新。原始材料在 raw/。）\n"_ustr))
        return false;
    if (!writeIfMissing(p.wiki + u"/log.md"_ustr, u"# 资料盘日志\n\n"_ustr))
        return false;
    if (!writeIfMissing(p.stateJson,
                        u"{\"schemaVersion\":\"vault-state/0.1\",\"autoCompile\":false,"
                        u"\"ftsIndexOnIngest\":true,\"officeLargeFile\":\"link\"}\n"_ustr))
        return false;
    return true;
}

bool VaultStore::isInitialized(const OUString& rRoot)
{
    const VaultPaths p = pathsFor(rRoot);
    return dirExists(p.raw) && dirExists(p.wiki) && dirExists(p.outputs);
}

bool VaultStore::autoCompileDefault() { return false; }

bool VaultStore::ftsIndexOnIngestDefault() { return true; }

OUString VaultStore::officeLargeFileDefault() { return u"link"_ustr; }

} // namespace kqoffice::ai::vault

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
