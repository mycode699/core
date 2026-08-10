/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include "VaultManager.hxx"
#include "VaultStore.hxx"

#include <PermissionCenter.hxx>

#include <osl/file.hxx>
#include <osl/process.h>
#include <osl/time.h>
#include <rtl/ustrbuf.hxx>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace kqoffice::ai::vault
{
namespace
{
OUString Home()
{
    const char* h = std::getenv("HOME");
    if (h && *h)
        return OUString::fromUtf8(h);
    return u"/tmp"_ustr;
}

OUString ConfigBase() { return Home() + u"/.config/kqoffice"_ustr; }

bool EnsureDir(const OUString& rSys)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return false;
    const auto rc = osl::Directory::createPath(url);
    return rc == osl::FileBase::E_None || rc == osl::FileBase::E_EXIST;
}

bool DirExists(const OUString& rSys)
{
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return false;
    osl::DirectoryItem item;
    if (osl::DirectoryItem::get(url, item) != osl::FileBase::E_None)
        return false;
    osl::FileStatus st(osl_FileStatus_Mask_Type);
    if (item.getFileStatus(st) != osl::FileBase::E_None)
        return false;
    return st.getFileType() == osl::FileStatus::Directory;
}

bool WriteUtf8(const OUString& rSys, const OUString& rBody)
{
    if (!EnsureDir(rSys.copy(0, std::max<sal_Int32>(0, rSys.lastIndexOf('/')))))
        return false;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(rSys, url) != osl::FileBase::E_None)
        return false;
    osl::File f(url);
    auto e = f.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create);
    if (e != osl::FileBase::E_None)
        e = f.open(osl_File_OpenFlag_Write);
    if (e != osl::FileBase::E_None)
        return false;
    f.setSize(0);
    const OString b = OUStringToOString(rBody, RTL_TEXTENCODING_UTF8);
    sal_uInt64 n = 0;
    f.write(b.getStr(), b.getLength(), n);
    f.close();
    return true;
}

OUString ReadUtf8(const OUString& rSys)
{
    const OString p = OUStringToOString(rSys, RTL_TEXTENCODING_UTF8);
    std::ifstream in(p.getStr(), std::ios::binary);
    if (!in)
        return {};
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return OStringToOUString(std::string_view(s.data(), s.size()), RTL_TEXTENCODING_UTF8);
}

OUString NowSec()
{
    TimeValue tv{};
    osl_getSystemTime(&tv);
    return OUString::number(static_cast<sal_Int64>(tv.Seconds));
}

OUString JsonEsc(const OUString& s)
{
    OUStringBuffer b;
    for (sal_Int32 i = 0; i < s.getLength(); ++i)
    {
        const sal_Unicode c = s[i];
        if (c == u'"' || c == u'\\')
        {
            b.append(u'\\');
            b.append(c);
        }
        else if (c == u'\n')
            b.append(u"\\n"_ustr);
        else
            b.append(c);
    }
    return b.makeStringAndClear();
}

OUString JsonGetStr(const OUString& json, const OUString& key)
{
    const OUString pat = u"\""_ustr + key + u"\":\""_ustr;
    const sal_Int32 p = json.indexOf(pat);
    if (p < 0)
        return {};
    const sal_Int32 a = p + pat.getLength();
    sal_Int32 b = a;
    while (b < json.getLength())
    {
        if (json[b] == u'"' && (b == a || json[b - 1] != u'\\'))
            break;
        ++b;
    }
    if (b <= a)
        return {};
    return json.copy(a, b - a);
}

bool JsonGetBool(const OUString& json, const OUString& key, bool def = false)
{
    const OUString pat = u"\""_ustr + key + u"\":"_ustr;
    const sal_Int32 p = json.indexOf(pat);
    if (p < 0)
        return def;
    const sal_Int32 a = p + pat.getLength();
    if (json.indexOf(u"true"_ustr, a) == a)
        return true;
    if (json.indexOf(u"false"_ustr, a) == a)
        return false;
    return def;
}

struct Registry
{
    OUString activeId;
    bool installDefaultsApplied = false;
    bool onboardingAcked = false;
    std::vector<VaultRecord> vaults;
};

Registry LoadRegistry()
{
    Registry r;
    const OUString body = ReadUtf8(VaultManager::registryPath());
    if (body.isEmpty())
        return r;
    r.activeId = JsonGetStr(body, u"activeId"_ustr);
    r.installDefaultsApplied = JsonGetBool(body, u"installDefaultsApplied"_ustr, false);
    r.onboardingAcked = JsonGetBool(body, u"onboardingAcked"_ustr, false);

    // crude vault object split by "id":
    sal_Int32 pos = 0;
    while (pos < body.getLength())
    {
        const sal_Int32 idPos = body.indexOf(u"\"id\":\""_ustr, pos);
        if (idPos < 0)
            break;
        // find enclosing object roughly: from previous { before id to matching }
        sal_Int32 objStart = body.lastIndexOf(u'{', idPos);
        sal_Int32 objEnd = body.indexOf(u'}', idPos);
        if (objStart < 0 || objEnd < 0)
            break;
        // skip registry-level keys that aren't vaults
        const OUString slice = body.copy(objStart, objEnd - objStart + 1);
        VaultRecord v;
        v.id = JsonGetStr(slice, u"id"_ustr);
        v.name = JsonGetStr(slice, u"name"_ustr);
        v.rootPath = JsonGetStr(slice, u"root"_ustr);
        if (v.rootPath.isEmpty())
            v.rootPath = JsonGetStr(slice, u"rootPath"_ustr);
        v.kind = JsonGetStr(slice, u"kind"_ustr);
        v.createdAt = JsonGetStr(slice, u"createdAt"_ustr);
        v.permissionGranted = JsonGetBool(slice, u"permissionGranted"_ustr, false);
        // filter out non-vault objects (no root)
        if (!v.id.isEmpty() && !v.rootPath.isEmpty() && v.id != u"vault-registry"_ustr)
            r.vaults.push_back(v);
        pos = objEnd + 1;
    }
    return r;
}

bool SaveRegistry(const Registry& r)
{
    EnsureDir(VaultManager::registryDir());
    OUStringBuffer b;
    b.append(u"{\n  \"schemaVersion\": \"vault-registry/0.1\",\n"_ustr);
    b.append(u"  \"id\": \"vault-registry\",\n"_ustr);
    b.append(u"  \"activeId\": \""_ustr);
    b.append(JsonEsc(r.activeId));
    b.append(u"\",\n  \"installDefaultsApplied\": "_ustr);
    b.append(r.installDefaultsApplied ? u"true"_ustr : u"false"_ustr);
    b.append(u",\n  \"onboardingAcked\": "_ustr);
    b.append(r.onboardingAcked ? u"true"_ustr : u"false"_ustr);
    b.append(u",\n  \"vaults\": [\n"_ustr);
    for (size_t i = 0; i < r.vaults.size(); ++i)
    {
        const auto& v = r.vaults[i];
        b.append(u"    {\"id\":\""_ustr);
        b.append(JsonEsc(v.id));
        b.append(u"\",\"name\":\""_ustr);
        b.append(JsonEsc(v.name));
        b.append(u"\",\"root\":\""_ustr);
        b.append(JsonEsc(v.rootPath));
        b.append(u"\",\"kind\":\""_ustr);
        b.append(JsonEsc(v.kind));
        b.append(u"\",\"createdAt\":\""_ustr);
        b.append(JsonEsc(v.createdAt));
        b.append(u"\",\"permissionGranted\":"_ustr);
        b.append(v.permissionGranted ? u"true"_ustr : u"false"_ustr);
        b.append(u"}"_ustr);
        if (i + 1 < r.vaults.size())
            b.append(u","_ustr);
        b.append(u"\n"_ustr);
    }
    b.append(u"  ]\n}\n"_ustr);
    return WriteUtf8(VaultManager::registryPath(), b.makeStringAndClear());
}

VaultRecord* FindMut(Registry& r, const OUString& id)
{
    for (auto& v : r.vaults)
        if (v.id == id)
            return &v;
    return nullptr;
}

const VaultRecord* Find(const Registry& r, const OUString& id)
{
    for (const auto& v : r.vaults)
        if (v.id == id)
            return &v;
    return nullptr;
}

bool GrantFolder(const OUString& rPath)
{
    kqoffice::ai::control::PermissionCenter pc;
    pc.load();
    // FolderScan capability + directory grant (WPS/Quark: user-selected workspace, not full disk)
    (void)pc.grant(kqoffice::ai::control::CapabilityPermission::FolderScan);
    const bool ok = pc.grantDirectory(rPath, /*recursive*/ true);
    (void)pc.save();
    return ok;
}

bool IsGranted(const OUString& rPath)
{
    kqoffice::ai::control::PermissionCenter pc;
    pc.load();
    return pc.isPathAuthorized(rPath);
}

OUString MakeVaultId()
{
    return u"v-"_ustr + NowSec();
}
} // namespace

OUString VaultManager::registryDir() { return ConfigBase() + u"/vaults"_ustr; }

OUString VaultManager::registryPath() { return registryDir() + u"/registry.json"_ustr; }

OUString VaultManager::installDefaultRootSuggestion()
{
    if (const char* env = std::getenv("KQOFFICE_VAULT_INSTALL_DEFAULT"); env && *env)
        return OUString::fromUtf8(env);
    // Thunder/Quark/WPS style: user Documents, product-named folder
    const OUString docs = Home() + u"/Documents/可圈资料盘"_ustr;
    // Prefer Documents if parent exists
    if (DirExists(Home() + u"/Documents"_ustr) || EnsureDir(Home() + u"/Documents"_ustr))
        return docs;
    return VaultStore::defaultRootDir();
}

VaultManagerResult VaultManager::ensureInstallDefaults()
{
    VaultManagerResult r;
    EnsureDir(registryDir());
    Registry reg = LoadRegistry();

    if (reg.installDefaultsApplied && !reg.vaults.empty() && !reg.activeId.isEmpty())
    {
        // refresh permission flags
        for (auto& v : reg.vaults)
            v.permissionGranted = IsGranted(v.rootPath);
        (void)SaveRegistry(reg);
        // still ensure layout for active
        VaultStore::ensureLayout(activeRoot());
        r.ok = true;
        r.vaultId = reg.activeId;
        r.rootPath = activeRoot();
        r.messageZh = u"资料盘已就绪（安装默认）"_ustr;
        return r;
    }

    const OUString root = installDefaultRootSuggestion();
    if (!VaultStore::ensureLayout(root))
    {
        // fallback config vault
        const OUString fallback = VaultStore::defaultRootDir();
        if (!VaultStore::ensureLayout(fallback))
        {
            r.messageZh = u"无法创建默认资料盘目录"_ustr;
            return r;
        }
        VaultRecord v;
        v.id = u"default"_ustr;
        v.name = u"默认资料盘"_ustr;
        v.rootPath = fallback;
        v.kind = u"install-default"_ustr;
        v.createdAt = NowSec();
        v.permissionGranted = GrantFolder(fallback);
        reg.vaults.clear();
        reg.vaults.push_back(v);
        reg.activeId = v.id;
        reg.installDefaultsApplied = true;
        SaveRegistry(reg);
        r.ok = true;
        r.vaultId = v.id;
        r.rootPath = fallback;
        r.messageZh = u"已创建默认资料盘（配置目录）· 已申请文件夹权限"_ustr;
        return r;
    }

    VaultRecord v;
    v.id = u"default"_ustr;
    v.name = u"默认资料盘"_ustr;
    v.rootPath = root;
    v.kind = u"install-default"_ustr;
    v.createdAt = NowSec();
    v.permissionGranted = GrantFolder(root);
    // also grant legacy config path if different (migration)
    const OUString legacy = VaultStore::defaultRootDir();
    if (legacy != root)
        (void)GrantFolder(legacy);

    reg.vaults.clear();
    reg.vaults.push_back(v);
    reg.activeId = v.id;
    reg.installDefaultsApplied = true;
    if (!SaveRegistry(reg))
    {
        r.messageZh = u"资料盘注册表写入失败"_ustr;
        return r;
    }
    r.ok = true;
    r.vaultId = v.id;
    r.rootPath = root;
    r.messageZh = u"已创建默认资料盘："_ustr + root
                  + (v.permissionGranted ? u" · 已授权访问"_ustr : u" · 请在设置中授权文件夹"_ustr);
    return r;
}

std::vector<VaultRecord> VaultManager::listVaults()
{
    (void)ensureInstallDefaults();
    return LoadRegistry().vaults;
}

VaultRecord VaultManager::activeVault()
{
    (void)ensureInstallDefaults();
    const Registry reg = LoadRegistry();
    if (const auto* v = Find(reg, reg.activeId))
        return *v;
    if (!reg.vaults.empty())
        return reg.vaults.front();
    VaultRecord empty;
    empty.id = u"default"_ustr;
    empty.name = u"默认资料盘"_ustr;
    empty.rootPath = installDefaultRootSuggestion();
    empty.kind = u"install-default"_ustr;
    return empty;
}

OUString VaultManager::activeRoot()
{
    if (const char* env = std::getenv("KQOFFICE_VAULT_ROOT"); env && *env)
        return OUString::fromUtf8(env);
    return activeVault().rootPath;
}

VaultManagerResult VaultManager::createVault(const OUString& rName, const OUString& rRootPath)
{
    VaultManagerResult r;
    (void)ensureInstallDefaults();
    OUString root = rRootPath.trim();
    if (root.isEmpty())
        root = installDefaultRootSuggestion() + u"-"_ustr + NowSec();
    if (!VaultStore::ensureLayout(root))
    {
        r.messageZh = u"无法在该路径创建资料盘（检查权限与磁盘）"_ustr;
        return r;
    }
    Registry reg = LoadRegistry();
    VaultRecord v;
    v.id = MakeVaultId();
    v.name = rName.isEmpty() ? (u"资料盘 "_ustr + v.id) : rName;
    v.rootPath = root;
    v.kind = u"user-created"_ustr;
    v.createdAt = NowSec();
    v.permissionGranted = GrantFolder(root);
    reg.vaults.push_back(v);
    reg.activeId = v.id;
    if (!SaveRegistry(reg))
    {
        r.messageZh = u"注册表保存失败"_ustr;
        return r;
    }
    r.ok = true;
    r.vaultId = v.id;
    r.rootPath = root;
    r.messageZh = u"已创建资料盘「"_ustr + v.name + u"」\n路径："_ustr + root
                  + (v.permissionGranted ? u"\n权限：已授权文件夹访问"_ustr
                                         : u"\n权限：未授权 — 请 /授权资料盘"_ustr);
    return r;
}

VaultManagerResult VaultManager::switchVault(const OUString& rId)
{
    VaultManagerResult r;
    Registry reg = LoadRegistry();
    if (!Find(reg, rId))
    {
        r.messageZh = u"找不到资料盘 id="_ustr + rId;
        return r;
    }
    reg.activeId = rId;
    if (!SaveRegistry(reg))
    {
        r.messageZh = u"切换失败（注册表）"_ustr;
        return r;
    }
    const auto* v = Find(reg, rId);
    VaultStore::ensureLayout(v->rootPath);
    r.ok = true;
    r.vaultId = rId;
    r.rootPath = v->rootPath;
    r.messageZh = u"已切换到「"_ustr + v->name + u"」\n"_ustr + v->rootPath;
    return r;
}

VaultManagerResult VaultManager::setVaultLocation(const OUString& rId, const OUString& rNewRoot)
{
    VaultManagerResult r;
    if (rNewRoot.trim().isEmpty())
    {
        r.messageZh = u"请指定新路径（类似下载路径设置）"_ustr;
        return r;
    }
    Registry reg = LoadRegistry();
    OUString id = rId;
    if (id.isEmpty())
        id = reg.activeId;
    auto* v = FindMut(reg, id);
    if (!v)
    {
        r.messageZh = u"资料盘不存在"_ustr;
        return r;
    }
    const OUString newRoot = rNewRoot.trim();
    if (!VaultStore::ensureLayout(newRoot))
    {
        r.messageZh = u"新路径无法创建资料盘结构"_ustr;
        return r;
    }
    v->rootPath = newRoot;
    v->kind = u"user-relocated"_ustr;
    v->permissionGranted = GrantFolder(newRoot);
    if (!SaveRegistry(reg))
    {
        r.messageZh = u"保存路径失败"_ustr;
        return r;
    }
    r.ok = true;
    r.vaultId = id;
    r.rootPath = newRoot;
    r.messageZh = u"资料盘位置已更新（不自动迁移旧文件）\n新路径："_ustr + newRoot
                  + u"\n请将需要的材料重新收入，或手动拷贝 raw/\n"_ustr
                  + (v->permissionGranted ? u"权限：已授权"_ustr : u"权限：请重新授权"_ustr);
    return r;
}

VaultManagerResult VaultManager::authorizeVault(const OUString& rId)
{
    VaultManagerResult r;
    Registry reg = LoadRegistry();
    OUString id = rId.isEmpty() ? reg.activeId : rId;
    auto* v = FindMut(reg, id);
    if (!v)
    {
        r.messageZh = u"资料盘不存在"_ustr;
        return r;
    }
    v->permissionGranted = GrantFolder(v->rootPath);
    // grant FolderScan capability explicitly
    {
        kqoffice::ai::control::PermissionCenter pc;
        pc.load();
        (void)pc.request(kqoffice::ai::control::CapabilityPermission::FolderScan,
                         u"资料盘需要读取您选择的文件夹以收录与检索本地材料"_ustr);
        (void)pc.grant(kqoffice::ai::control::CapabilityPermission::FolderScan);
        (void)pc.save();
    }
    SaveRegistry(reg);
    r.ok = v->permissionGranted;
    r.vaultId = id;
    r.rootPath = v->rootPath;
    if (v->permissionGranted)
    {
        OUStringBuffer mb;
        mb.append(u"已授权资料盘文件夹：\n"_ustr);
        mb.append(v->rootPath);
        mb.append(u"\n（仅授权此目录，非整盘；可在权限设置中撤销）"_ustr);
        r.messageZh = mb.makeStringAndClear();
    }
    else
    {
        r.messageZh = u"授权失败：无法登记路径 "_ustr + v->rootPath;
    }
    return r;
}

VaultManagerResult VaultManager::revokeVaultAuth(const OUString& rId)
{
    VaultManagerResult r;
    Registry reg = LoadRegistry();
    OUString id = rId.isEmpty() ? reg.activeId : rId;
    auto* v = FindMut(reg, id);
    if (!v)
    {
        r.messageZh = u"资料盘不存在"_ustr;
        return r;
    }
    kqoffice::ai::control::PermissionCenter pc;
    pc.load();
    (void)pc.revokeDirectory(v->rootPath);
    (void)pc.save();
    v->permissionGranted = false;
    SaveRegistry(reg);
    r.ok = true;
    r.vaultId = id;
    r.messageZh = u"已撤销资料盘文件夹授权："_ustr + v->rootPath;
    return r;
}

bool VaultManager::isVaultAuthorized(const OUString& rId)
{
    const Registry reg = LoadRegistry();
    const OUString id = rId.isEmpty() ? reg.activeId : rId;
    const auto* v = Find(reg, id);
    if (!v)
        return false;
    return IsGranted(v->rootPath);
}

VaultManagerResult VaultManager::renameVault(const OUString& rId, const OUString& rNewName)
{
    VaultManagerResult r;
    if (rNewName.trim().isEmpty())
    {
        r.messageZh = u"名称不能为空"_ustr;
        return r;
    }
    Registry reg = LoadRegistry();
    auto* v = FindMut(reg, rId);
    if (!v)
    {
        r.messageZh = u"资料盘不存在"_ustr;
        return r;
    }
    v->name = rNewName.trim();
    SaveRegistry(reg);
    r.ok = true;
    r.vaultId = rId;
    r.messageZh = u"已重命名为「"_ustr + v->name + u"」"_ustr;
    return r;
}

VaultManagerResult VaultManager::revealInFileManager(const OUString& rId)
{
    VaultManagerResult r;
    const Registry reg = LoadRegistry();
    const OUString id = rId.isEmpty() ? reg.activeId : rId;
    const auto* v = Find(reg, id);
    if (!v)
    {
        r.messageZh = u"资料盘不存在"_ustr;
        return r;
    }
    VaultStore::ensureLayout(v->rootPath);
#if defined(MACOSX)
    const OString cmd = "open "
                        + OUStringToOString(v->rootPath, RTL_TEXTENCODING_UTF8);
#else
    const OString cmd = "xdg-open "
                        + OUStringToOString(v->rootPath, RTL_TEXTENCODING_UTF8)
                        + " >/dev/null 2>&1 &";
#endif
    (void)std::system(cmd.getStr());
    r.ok = true;
    r.rootPath = v->rootPath;
    r.messageZh = u"已在文件管理器中打开："_ustr + v->rootPath;
    return r;
}

OUString VaultManager::managementSummaryZh()
{
    (void)ensureInstallDefaults();
    const Registry reg = LoadRegistry();
    const VaultRecord act = activeVault();
    OUStringBuffer b;
    b.append(u"## 资料盘管理\n\n"_ustr);
    b.append(u"**产品理念：** 材料进盘 · 本机检索 · 写稿引用 · 批准才写回。"
             u"默认位置可改、按目录授权、可多盘切换——像管理下载路径一样管理知识资产。\n\n"_ustr);
    b.append(u"**当前**：「"_ustr);
    b.append(act.name);
    b.append(u"」\n"_ustr);
    b.append(u"- 路径：`"_ustr);
    b.append(act.rootPath);
    b.append(u"`\n"_ustr);
    b.append(u"- 权限："_ustr);
    b.append(IsGranted(act.rootPath) ? u"已授权"_ustr : u"未授权（/授权资料盘）"_ustr);
    b.append(u"\n- 类型："_ustr);
    b.append(act.kind);
    b.append(u"\n\n**全部资料盘**\n"_ustr);
    for (const auto& v : reg.vaults)
    {
        b.append(v.id == reg.activeId ? u"- ● "_ustr : u"- ○ "_ustr);
        b.append(v.name);
        b.append(u" (`"_ustr);
        b.append(v.id);
        b.append(u"`)\n  "_ustr);
        b.append(v.rootPath);
        b.append(IsGranted(v.rootPath) ? u" · 已授权\n"_ustr : u" · 未授权\n"_ustr);
    }
    b.append(u"\n**命令**\n"_ustr);
    b.append(u"- `/新建资料盘 名称 | /路径`\n"_ustr);
    b.append(u"- `/切换资料盘 <id>`\n"_ustr);
    b.append(u"- `/资料盘位置 /新路径` — 改默认路径（不自动搬家）\n"_ustr);
    b.append(u"- `/授权资料盘` · `/撤销资料盘授权`\n"_ustr);
    b.append(u"- `/打开资料盘` — Finder/文件管理器\n"_ustr);
    b.append(u"- `/资料盘初始化` — 重跑安装默认\n\n"_ustr);
    b.append(u"权限说明：仅授权您选择的资料盘目录（及子目录），**不会**默认获取整盘访问。\n"_ustr);
    return b.makeStringAndClear();
}

OUString VaultManager::statusChipZh()
{
    (void)ensureInstallDefaults();
    const auto v = activeVault();
    OUStringBuffer b;
    b.append(u"资料盘 · "_ustr);
    b.append(v.name);
    b.append(IsGranted(v.rootPath) ? u" · 已授权"_ustr : u" · 待授权"_ustr);
    return b.makeStringAndClear();
}

bool VaultManager::setOnboardingAcked(bool bAcked)
{
    Registry reg = LoadRegistry();
    reg.onboardingAcked = bAcked;
    return SaveRegistry(reg);
}

bool VaultManager::isOnboardingAcked() { return LoadRegistry().onboardingAcked; }

} // namespace kqoffice::ai::vault
