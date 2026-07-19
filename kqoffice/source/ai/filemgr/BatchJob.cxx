/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈办公 project (Wave D6: batch convert / archive).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "BatchJob.hxx"

#include "AIFileManager.hxx"
#include "PermissionCenter.hxx"

#include <com/sun/star/beans/PropertyValue.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/XStorable.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/uno/Reference.hxx>
#include <com/sun/star/uno/Sequence.hxx>
#include <com/sun/star/uno/Exception.hpp>

#include <comphelper/processfactory.hxx>
#include <comphelper/propertyvalue.hxx>

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <utility>
#include <vector>
#include <osl/file.hxx>
#include <osl/time.h>
#include <rtl/bootstrap.hxx>
#include <sal/log.hxx>

namespace kqoffice::ai::filemgr
{

namespace
{

OUString toFileUrl(const OUString& systemOrUrl)
{
    if (systemOrUrl.startsWith("file://"))
        return systemOrUrl;
    OUString url;
    if (osl::FileBase::getFileURLFromSystemPath(systemOrUrl, url) == osl::FileBase::E_None
        && !url.isEmpty())
        return url;
    return systemOrUrl;
}

sal_Int64 currentTimeMs()
{
    TimeValue tv;
    osl_getSystemTime(&tv);
    return static_cast<sal_Int64>(tv.Seconds) * 1000
         + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
}

OUString fileStem(const OUString& path)
{
    sal_Int32 slash = path.lastIndexOf('/');
    OUString name = (slash >= 0) ? path.copy(slash + 1) : path;
    sal_Int32 dot = name.lastIndexOf('.');
    if (dot > 0)
        return name.copy(0, dot);
    return name;
}

OUString parentDir(const OUString& path)
{
    sal_Int32 slash = path.lastIndexOf('/');
    if (slash <= 0)
        return OUString();
    return path.copy(0, slash);
}

bool writeTextFile(const OUString& path, const OUString& content)
{
    const OUString fileUrl = toFileUrl(path);
    osl::File::remove(fileUrl);
    osl::File out(fileUrl);
    if (out.open(osl_File_OpenFlag_Write | osl_File_OpenFlag_Create) != osl::FileBase::E_None)
        return false;
    const OString utf8 = OUStringToOString(content, RTL_TEXTENCODING_UTF8);
    sal_uInt64 written = 0;
    out.write(utf8.getStr(), utf8.getLength(), written);
    out.close();
    return written > 0 || content.isEmpty();
}

OUString escapeTsvField(const OUString& s)
{
    OUString out = s;
    out = out.replaceAll(u"\t"_ustr, u" "_ustr);
    out = out.replaceAll(u"\n"_ustr, u" "_ustr);
    out = out.replaceAll(u"\r"_ustr, u" "_ustr);
    return out;
}

} // namespace

// ── BatchJob move ───────────────────────────────────────────────────────

BatchJob::BatchJob(BatchJob&& other) noexcept
    : id(std::move(other.id))
    , kind(other.kind)
    , state(other.state)
    , items(std::move(other.items))
    , reasonZh(std::move(other.reasonZh))
    , createdAtMs(other.createdAtMs)
    , startedAtMs(other.startedAtMs)
    , finishedAtMs(other.finishedAtMs)
    , ledgerPath(std::move(other.ledgerPath))
    , cancelRequested(other.cancelRequested.load())
{
}

BatchJob& BatchJob::operator=(BatchJob&& other) noexcept
{
    if (this != &other)
    {
        id = std::move(other.id);
        kind = other.kind;
        state = other.state;
        items = std::move(other.items);
        reasonZh = std::move(other.reasonZh);
        createdAtMs = other.createdAtMs;
        startedAtMs = other.startedAtMs;
        finishedAtMs = other.finishedAtMs;
        ledgerPath = std::move(other.ledgerPath);
        cancelRequested.store(other.cancelRequested.load());
    }
    return *this;
}

// ── BatchJobManager ─────────────────────────────────────────────────────

BatchJobManager::BatchJobManager() = default;
BatchJobManager::~BatchJobManager() = default;

BatchJob BatchJobManager::create(BatchJobKind kind) const
{
    BatchJob job;
    job.kind = kind;
    job.state = BatchJobState::Pending;
    job.createdAtMs = currentTimeMs();
    job.id = u"batch-"_ustr + OUString::number(job.createdAtMs) + u"-"_ustr
             + OUString::number(static_cast<sal_uInt32>(kind));
    return job;
}

bool BatchJobManager::addItem(BatchJob& job, const OUString& sourcePath,
                              const OUString& targetPath) const
{
    if (sourcePath.isEmpty())
        return false;
    if (job.state != BatchJobState::Pending)
        return false;

    BatchItem item;
    item.sourcePath = sourcePath;
    item.exportFilter = defaultExportFilter(job.kind);
    if (job.kind == BatchJobKind::SoftDeleteToTrash)
    {
        item.targetPath.clear();
    }
    else if (!targetPath.isEmpty())
    {
        item.targetPath = targetPath;
    }
    else
    {
        item.targetPath = deriveTargetPath(sourcePath, job.kind);
    }
    job.items.push_back(std::move(item));
    return true;
}

void BatchJobManager::requestCancel(BatchJob& job)
{
    job.cancelRequested.store(true);
}

OUString BatchJobManager::defaultExportFilter(BatchJobKind kind)
{
    switch (kind)
    {
        case BatchJobKind::ConvertToPdf:
            // Default PDF export filter name passed to XStorable::storeToURL.
            // Module-specific calc/impress PDF filters can be set per item later.
            return u"writer_pdf_Export"_ustr;
        case BatchJobKind::ConvertToDocx:
            return u"MS Word 2007 XML"_ustr;
        case BatchJobKind::ConvertToXlsx:
            return u"Calc MS Excel 2007 XML"_ustr;
        case BatchJobKind::ConvertToPptx:
            return u"Impress MS PowerPoint 2007 XML"_ustr;
        case BatchJobKind::SoftDeleteToTrash:
            return OUString();
    }
    return OUString();
}

OUString BatchJobManager::defaultTargetExtension(BatchJobKind kind)
{
    switch (kind)
    {
        case BatchJobKind::ConvertToPdf:
            return u".pdf"_ustr;
        case BatchJobKind::ConvertToDocx:
            return u".docx"_ustr;
        case BatchJobKind::ConvertToXlsx:
            return u".xlsx"_ustr;
        case BatchJobKind::ConvertToPptx:
            return u".pptx"_ustr;
        case BatchJobKind::SoftDeleteToTrash:
            return OUString();
    }
    return OUString();
}

OUString BatchJobManager::deriveTargetPath(const OUString& sourcePath, BatchJobKind kind)
{
    const OUString ext = defaultTargetExtension(kind);
    if (ext.isEmpty() || sourcePath.isEmpty())
        return OUString();
    const OUString dir = parentDir(sourcePath);
    const OUString stem = fileStem(sourcePath);
    if (dir.isEmpty())
        return stem + ext;
    return dir + u"/"_ustr + stem + ext;
}

OUString BatchJobManager::kindLabelZh(BatchJobKind kind)
{
    switch (kind)
    {
        case BatchJobKind::ConvertToPdf:
            return u"批量转换为 PDF"_ustr;
        case BatchJobKind::ConvertToDocx:
            return u"批量转换为 DOCX"_ustr;
        case BatchJobKind::ConvertToXlsx:
            return u"批量转换为 XLSX"_ustr;
        case BatchJobKind::ConvertToPptx:
            return u"批量转换为 PPTX"_ustr;
        case BatchJobKind::SoftDeleteToTrash:
            return u"批量移入回收站"_ustr;
    }
    return u"未知批量任务"_ustr;
}

OUString BatchJobManager::jobStateLabelZh(BatchJobState state)
{
    switch (state)
    {
        case BatchJobState::Pending:
            return u"待执行"_ustr;
        case BatchJobState::Running:
            return u"执行中"_ustr;
        case BatchJobState::Done:
            return u"已完成"_ustr;
        case BatchJobState::Failed:
            return u"失败"_ustr;
        case BatchJobState::Cancelled:
            return u"已取消"_ustr;
    }
    return u"未知"_ustr;
}

OUString BatchJobManager::itemStateLabelZh(BatchItemState state)
{
    switch (state)
    {
        case BatchItemState::Pending:
            return u"待执行"_ustr;
        case BatchItemState::Running:
            return u"执行中"_ustr;
        case BatchItemState::Done:
            return u"已完成"_ustr;
        case BatchItemState::Failed:
            return u"失败"_ustr;
        case BatchItemState::Cancelled:
            return u"已取消"_ustr;
        case BatchItemState::Skipped:
            return u"已跳过"_ustr;
    }
    return u"未知"_ustr;
}

OUString BatchJobManager::ledgerRootDir()
{
    // Prefer the same override used by AIFileManager tests/workbench.
    const char* env = std::getenv("KQOFFICE_AI_FILEMGR_DIR");
    if (env && *env)
        return OUString::createFromAscii(env) + u"/batch-jobs"_ustr;
    return AIFileManager::workbenchStoreDir() + u"/batch-jobs"_ustr;
}

namespace
{
OUString ledgerTsvField(const OUString& body, std::u16string_view key)
{
    const OUString needle = OUString::Concat(key) + u"\t";
    sal_Int32 p = body.indexOf(needle);
    if (p < 0)
        return OUString();
    sal_Int32 start = p + needle.getLength();
    sal_Int32 end = body.indexOf(u'\n', start);
    if (end < 0)
        end = body.getLength();
    return body.copy(start, end - start).trim();
}

OUString readLedgerFileUtf8(const OUString& systemPath)
{
    const OUString url = toFileUrl(systemPath);
    osl::File f(url);
    if (f.open(osl_File_OpenFlag_Read) != osl::FileBase::E_None)
        return OUString();
    sal_uInt64 sz = 0;
    f.getSize(sz);
    if (sz == 0 || sz > 512 * 1024)
    {
        f.close();
        return OUString();
    }
    std::vector<char> buf(static_cast<size_t>(sz));
    sal_uInt64 n = 0;
    f.read(buf.data(), sz, n);
    f.close();
    if (n == 0)
        return OUString();
    return OUString::fromUtf8(std::string_view(buf.data(), static_cast<size_t>(n)));
}
} // namespace

std::vector<BatchJobLedgerSummary> BatchJobManager::listRecentLedgers(sal_Int32 maxCount)
{
    std::vector<BatchJobLedgerSummary> out;
    if (maxCount <= 0)
        return out;

    const OUString root = ledgerRootDir();
    const OUString url = toFileUrl(root);
    if (url.isEmpty())
        return out;

    osl::Directory dir(url);
    if (dir.open() != osl::FileBase::E_None)
        return out;

    struct Entry
    {
        OUString path;
        sal_Int64 mtime = 0;
    };
    std::vector<Entry> files;

    osl::DirectoryItem item;
    while (dir.getNextItem(item) == osl::FileBase::E_None)
    {
        osl::FileStatus st(osl_FileStatus_Mask_Type | osl_FileStatus_Mask_FileName
                           | osl_FileStatus_Mask_FileURL | osl_FileStatus_Mask_ModifyTime);
        if (item.getFileStatus(st) != osl::FileBase::E_None)
            continue;
        if (!st.isValid(osl_FileStatus_Mask_Type) || !st.isRegular())
            continue;
        if (!st.isValid(osl_FileStatus_Mask_FileName))
            continue;
        const OUString name = st.getFileName();
        if (!name.endsWithIgnoreAsciiCase(u".tsv"))
            continue;
        OUString sysPath;
        if (st.isValid(osl_FileStatus_Mask_FileURL))
            osl::FileBase::getSystemPathFromFileURL(st.getFileURL(), sysPath);
        if (sysPath.isEmpty())
            sysPath = root + u"/"_ustr + name;
        Entry e;
        e.path = sysPath;
        if (st.isValid(osl_FileStatus_Mask_ModifyTime))
        {
            TimeValue tv = st.getModifyTime();
            e.mtime = static_cast<sal_Int64>(tv.Seconds) * 1000
                      + static_cast<sal_Int64>(tv.Nanosec) / 1000000;
        }
        files.push_back(std::move(e));
    }

    std::sort(files.begin(), files.end(),
              [](const Entry& a, const Entry& b) { return a.mtime > b.mtime; });

    const sal_Int32 limit = std::min(maxCount, static_cast<sal_Int32>(files.size()));
    out.reserve(static_cast<size_t>(limit));
    for (sal_Int32 i = 0; i < limit; ++i)
    {
        const OUString body = readLedgerFileUtf8(files[static_cast<size_t>(i)].path);
        if (body.isEmpty())
            continue;
        BatchJobLedgerSummary s;
        s.ledgerPath = files[static_cast<size_t>(i)].path;
        s.id = ledgerTsvField(body, u"id");
        s.kindZh = ledgerTsvField(body, u"kind");
        s.stateZh = ledgerTsvField(body, u"state");
        s.reasonZh = ledgerTsvField(body, u"reasonZh");
        const OUString fin = ledgerTsvField(body, u"finishedAtMs");
        if (!fin.isEmpty())
            s.finishedAtMs = fin.toInt64();
        if (s.finishedAtMs <= 0)
            s.finishedAtMs = files[static_cast<size_t>(i)].mtime;
        if (s.id.isEmpty())
        {
            // Fall back to filename stem.
            const sal_Int32 slash = s.ledgerPath.lastIndexOf(u'/');
            OUString base = slash >= 0 ? s.ledgerPath.copy(slash + 1) : s.ledgerPath;
            if (base.endsWithIgnoreAsciiCase(u".tsv"))
                base = base.copy(0, base.getLength() - 4);
            s.id = base;
        }
        if (s.kindZh.isEmpty())
            s.kindZh = u"批量"_ustr;
        if (s.stateZh.isEmpty())
            s.stateZh = u"—"_ustr;
        out.push_back(std::move(s));
    }
    return out;
}

bool BatchJobManager::pathExists(const OUString& systemPath)
{
    if (systemPath.isEmpty())
        return false;
    osl::DirectoryItem item;
    if (osl::DirectoryItem::get(toFileUrl(systemPath), item) != osl::FileBase::E_None)
        return false;
    osl::FileStatus st(osl_FileStatus_Mask_Type);
    if (item.getFileStatus(st) != osl::FileBase::E_None)
        return false;
    return st.isValid(osl_FileStatus_Mask_Type);
}

bool BatchJobManager::isOfficeProcessAvailable()
{
    // KQOFFICE_BATCH_UNO=0 forces the stub fail path (unit tests / headless CI).
    // Default: probe the live process component context + Desktop service.
    const char* env = std::getenv("KQOFFICE_BATCH_UNO");
    if (env && env[0] == '0')
        return false;

    try
    {
        const css::uno::Reference<css::uno::XComponentContext> xCtx(
            comphelper::getProcessComponentContext());
        if (!xCtx.is())
            return false;
        const css::uno::Reference<css::frame::XDesktop2> xDesktop(
            css::frame::Desktop::create(xCtx));
        return xDesktop.is();
    }
    catch (const css::uno::Exception&)
    {
        return false;
    }
    catch (...)
    {
        return false;
    }
}

bool BatchJobManager::writeLedger(const BatchJob& job) const
{
    if (job.ledgerPath.isEmpty())
        return false;

    const OUString dir = parentDir(job.ledgerPath);
    if (!dir.isEmpty())
        osl::Directory::createPath(toFileUrl(dir));

    OUString body;
    body += u"# kqoffice batch job ledger\n"_ustr;
    body += u"id\t"_ustr + job.id + u"\n"_ustr;
    body += u"kind\t"_ustr + kindLabelZh(job.kind) + u"\n"_ustr;
    body += u"state\t"_ustr + jobStateLabelZh(job.state) + u"\n"_ustr;
    body += u"createdAtMs\t"_ustr + OUString::number(job.createdAtMs) + u"\n"_ustr;
    body += u"startedAtMs\t"_ustr + OUString::number(job.startedAtMs) + u"\n"_ustr;
    body += u"finishedAtMs\t"_ustr + OUString::number(job.finishedAtMs) + u"\n"_ustr;
    body += u"reasonZh\t"_ustr + escapeTsvField(job.reasonZh) + u"\n"_ustr;
    body += u"# source\ttarget\tfilter\tstate\treasonZh\n"_ustr;
    for (const auto& item : job.items)
    {
        body += escapeTsvField(item.sourcePath) + u"\t"_ustr
              + escapeTsvField(item.targetPath) + u"\t"_ustr
              + escapeTsvField(item.exportFilter) + u"\t"_ustr
              + itemStateLabelZh(item.state) + u"\t"_ustr
              + escapeTsvField(item.reasonZh) + u"\n"_ustr;
    }
    return writeTextFile(job.ledgerPath, body);
}

void BatchJobManager::processSoftDelete(
    BatchItem& item, kqoffice::ai::control::PermissionDecision confirm) const
{
    // AIFileManager::moveToTrash enforces isPathAuthorized + resolveRiskyOp(Delete).
    AIFileManager mgr;
    const OUString note = u"批量归档 SoftDeleteToTrash"_ustr;
    if (mgr.moveToTrash(item.sourcePath, note, confirm))
    {
        item.state = BatchItemState::Done;
        item.reasonZh = u"已移入产品回收站"_ustr;
    }
    else
    {
        item.state = BatchItemState::Failed;
        kqoffice::ai::control::PermissionCenter perms;
        if (!perms.isPathAuthorized(item.sourcePath))
            item.reasonZh = u"路径未授权，拒绝移入回收站"_ustr;
        else if (confirm == kqoffice::ai::control::PermissionDecision::Deny
                 && !perms.hasSessionRiskGrant(
                        kqoffice::ai::control::RiskOperation::Delete, item.sourcePath))
            item.reasonZh = u"用户拒绝删除确认（拒绝/本次/本轮）"_ustr;
        else
            item.reasonZh = u"移入回收站失败（文件不存在或写入回收站出错）"_ustr;
    }
}

void BatchJobManager::processConvert(
    BatchItem& item, BatchJobKind kind,
    kqoffice::ai::control::PermissionDecision confirm) const
{
    kqoffice::ai::control::PermissionCenter perms;

    if (!perms.isPathAuthorized(item.sourcePath))
    {
        item.state = BatchItemState::Failed;
        item.reasonZh = u"源路径未授权，拒绝转换"_ustr;
        return;
    }
    if (!pathExists(item.sourcePath))
    {
        item.state = BatchItemState::Failed;
        item.reasonZh = u"源文件不存在"_ustr;
        return;
    }

    if (item.targetPath.isEmpty())
        item.targetPath = deriveTargetPath(item.sourcePath, kind);
    if (item.exportFilter.isEmpty())
        item.exportFilter = defaultExportFilter(kind);

    // Target must sit under an authorized root as well.
    if (!perms.isPathAuthorized(item.targetPath))
    {
        item.state = BatchItemState::Failed;
        item.reasonZh = u"目标路径未授权，拒绝写出转换结果"_ustr;
        return;
    }

    if (pathExists(item.targetPath))
    {
        if (!perms.resolveRiskyOp(kqoffice::ai::control::RiskOperation::Overwrite,
                                  item.targetPath, confirm))
        {
            item.state = BatchItemState::Failed;
            item.reasonZh = u"目标已存在且用户拒绝覆盖确认（拒绝/本次/本轮）"_ustr;
            return;
        }
    }

    // Stub path: env KQOFFICE_BATCH_UNO=0, or no process component context/Desktop.
    if (!isOfficeProcessAvailable())
    {
        item.state = BatchItemState::Failed;
        item.reasonZh = u"无可用 Office 进程，转换未执行（已记录导出过滤器："_ustr
                        + item.exportFilter + u"）"_ustr;
        SAL_INFO("kqoffice.ai.filemgr",
                 "BatchJob convert stub fail filter=" << item.exportFilter
                     << " src=" << item.sourcePath << " dst=" << item.targetPath);
        return;
    }

    // Real UNO convert: hidden load → storeToURL(FilterName) → dispose.
    css::uno::Reference<css::lang::XComponent> xComponent;
    try
    {
        const css::uno::Reference<css::uno::XComponentContext> xCtx(
            comphelper::getProcessComponentContext());
        const css::uno::Reference<css::frame::XDesktop2> xDesktop(
            css::frame::Desktop::create(xCtx));

        const OUString srcUrl = toFileUrl(item.sourcePath);
        const OUString dstUrl = toFileUrl(item.targetPath);

        css::uno::Sequence<css::beans::PropertyValue> loadArgs{
            comphelper::makePropertyValue(u"Hidden"_ustr, true),
            comphelper::makePropertyValue(u"ReadOnly"_ustr, true),
        };

        xComponent = xDesktop->loadComponentFromURL(srcUrl, u"_blank"_ustr, 0, loadArgs);
        if (!xComponent.is())
        {
            item.state = BatchItemState::Failed;
            item.reasonZh = u"无法加载源文档，转换未执行"_ustr;
            return;
        }

        css::uno::Reference<css::frame::XStorable> xStore(xComponent, css::uno::UNO_QUERY);
        if (!xStore.is())
        {
            item.state = BatchItemState::Failed;
            item.reasonZh = u"文档不支持导出存储接口，无法转换"_ustr;
            try
            {
                xComponent->dispose();
            }
            catch (const css::uno::Exception&)
            {
            }
            xComponent.clear();
            return;
        }

        // Overwrite already permission-gated above when target existed.
        css::uno::Sequence<css::beans::PropertyValue> storeArgs{
            comphelper::makePropertyValue(u"FilterName"_ustr, item.exportFilter),
            comphelper::makePropertyValue(u"Overwrite"_ustr, true),
        };
        xStore->storeToURL(dstUrl, storeArgs);

        item.state = BatchItemState::Done;
        item.reasonZh = u"转换成功（过滤器："_ustr + item.exportFilter + u"）"_ustr;
        SAL_INFO("kqoffice.ai.filemgr",
                 "BatchJob convert ok filter=" << item.exportFilter
                     << " src=" << item.sourcePath << " dst=" << item.targetPath);
    }
    catch (const css::uno::Exception& e)
    {
        item.state = BatchItemState::Failed;
        item.reasonZh = u"UNO 转换失败："_ustr + e.Message;
        SAL_WARN("kqoffice.ai.filemgr",
                 "BatchJob convert UNO exception: " << e.Message
                     << " filter=" << item.exportFilter
                     << " src=" << item.sourcePath << " dst=" << item.targetPath);
    }
    catch (...)
    {
        item.state = BatchItemState::Failed;
        item.reasonZh = u"UNO 转换失败：未知异常"_ustr;
        SAL_WARN("kqoffice.ai.filemgr",
                 "BatchJob convert unknown exception filter=" << item.exportFilter
                     << " src=" << item.sourcePath << " dst=" << item.targetPath);
    }

    if (xComponent.is())
    {
        try
        {
            xComponent->dispose();
        }
        catch (const css::uno::Exception&)
        {
        }
        xComponent.clear();
    }
}

void BatchJobManager::processItem(
    BatchJob& job, BatchItem& item,
    kqoffice::ai::control::PermissionDecision confirm) const
{
    item.state = BatchItemState::Running;
    item.startedAtMs = currentTimeMs();
    item.reasonZh.clear();

    switch (job.kind)
    {
        case BatchJobKind::ConvertToPdf:
        case BatchJobKind::ConvertToDocx:
        case BatchJobKind::ConvertToXlsx:
        case BatchJobKind::ConvertToPptx:
            processConvert(item, job.kind, confirm);
            break;
        case BatchJobKind::SoftDeleteToTrash:
            processSoftDelete(item, confirm);
            break;
    }

    item.finishedAtMs = currentTimeMs();
}

BatchRunSummary BatchJobManager::run(
    BatchJob& job, kqoffice::ai::control::PermissionDecision confirm) const
{
    BatchRunSummary summary;
    summary.total = static_cast<sal_Int32>(job.items.size());

    if (job.state != BatchJobState::Pending && job.state != BatchJobState::Failed
        && job.state != BatchJobState::Cancelled)
    {
        // Only allow (re)run from Pending; Failed/Cancelled reset to re-run remaining.
        if (job.state == BatchJobState::Running || job.state == BatchJobState::Done)
        {
            job.reasonZh = u"任务状态不允许再次执行"_ustr;
            summary.allSucceeded = false;
            return summary;
        }
    }

    job.state = BatchJobState::Running;
    job.startedAtMs = currentTimeMs();
    job.finishedAtMs = 0;
    job.reasonZh.clear();
    job.ledgerPath = ledgerRootDir() + u"/"_ustr + job.id + u".tsv"_ustr;

    bool sawCancel = false;
    for (auto& item : job.items)
    {
        if (job.cancelRequested.load())
        {
            sawCancel = true;
            if (item.state == BatchItemState::Pending
                || item.state == BatchItemState::Running)
            {
                item.state = BatchItemState::Cancelled;
                item.reasonZh = u"任务已取消"_ustr;
                item.finishedAtMs = currentTimeMs();
            }
            continue;
        }

        // Skip already-done items on re-run.
        if (item.state == BatchItemState::Done)
            continue;

        processItem(job, item, confirm);

        // Cooperative cancel checked between items (and if set mid-item, next ones cancel).
        if (job.cancelRequested.load())
            sawCancel = true;
    }

    for (const auto& item : job.items)
    {
        switch (item.state)
        {
            case BatchItemState::Done:
                ++summary.done;
                break;
            case BatchItemState::Failed:
                ++summary.failed;
                break;
            case BatchItemState::Cancelled:
                ++summary.cancelled;
                break;
            case BatchItemState::Skipped:
                ++summary.skipped;
                break;
            default:
                break;
        }
    }

    job.finishedAtMs = currentTimeMs();
    if (sawCancel || summary.cancelled > 0)
    {
        job.state = BatchJobState::Cancelled;
        job.reasonZh = u"批量任务已取消（完成 "_ustr + OUString::number(summary.done)
                       + u" / 失败 "_ustr + OUString::number(summary.failed)
                       + u" / 取消 "_ustr + OUString::number(summary.cancelled) + u"）"_ustr;
    }
    else if (summary.failed > 0)
    {
        job.state = BatchJobState::Failed;
        job.reasonZh = u"批量任务部分或全部失败（成功 "_ustr + OUString::number(summary.done)
                       + u" / 失败 "_ustr + OUString::number(summary.failed) + u"）"_ustr;
    }
    else if (summary.total == 0)
    {
        job.state = BatchJobState::Done;
        job.reasonZh = u"批量任务无条目，视为完成"_ustr;
        summary.allSucceeded = true;
    }
    else
    {
        job.state = BatchJobState::Done;
        job.reasonZh = u"批量任务全部完成（"_ustr + OUString::number(summary.done) + u" 项）"_ustr;
        summary.allSucceeded = (summary.done == summary.total);
    }

    if (!writeLedger(job))
    {
        SAL_WARN("kqoffice.ai.filemgr", "BatchJob: failed to write ledger " << job.ledgerPath);
        if (job.reasonZh.isEmpty())
            job.reasonZh = u"任务台账写入失败"_ustr;
        else
            job.reasonZh += u"；台账写入失败"_ustr;
    }

    return summary;
}

} // namespace kqoffice::ai::filemgr

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
