/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V5: AI Canvas Mode).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * AICanvasEntryPoint — UI entry for launching canvas mode.
 */

#include "AICanvasEntryPoint.hxx"
#include "AICanvasIntegration.hxx"

#include <sal/log.hxx>

namespace kqoffice::ai::canvas
{

bool AICanvasEntryPoint::launchCanvasMode(CanvasDocType docType)
{
    SAL_INFO("kqoffice.ai.canvas",
             "launchCanvasMode: type=" << static_cast<int>(docType));

    OUString goal = u"新建"_ustr + getDisplayName(docType);
    return AICanvasIntegration::startCanvasViaChat(docType, goal);
}

bool AICanvasEntryPoint::launchCanvasModeForExisting(const OUString& documentUrl)
{
    (void)documentUrl;
    // Default to Writer canvas for existing documents
    return launchCanvasMode(CanvasDocType::Writer);
}

bool AICanvasEntryPoint::isAvailable(CanvasDocType)
{
    return true;
}

OUString AICanvasEntryPoint::getDisplayName(CanvasDocType docType)
{
    switch (docType)
    {
        case CanvasDocType::Writer:  return u"AI 画布"_ustr;
        case CanvasDocType::Calc:    return u"AI 表格画布"_ustr;
        case CanvasDocType::Impress: return u"AI 演示画布"_ustr;
    }
    return u"AI 画布"_ustr;
}

OUString AICanvasEntryPoint::getDescription(CanvasDocType docType)
{
    switch (docType)
    {
        case CanvasDocType::Writer:
            return u"用自然语言描述想要的文档，AI 逐步帮你创建完成"_ustr;
        case CanvasDocType::Calc:
            return u"描述数据表格结构，AI 逐步生成表格和公式"_ustr;
        case CanvasDocType::Impress:
            return u"描述演示内容，AI 逐步生成幻灯片"_ustr;
    }
    return u""_ustr;
}

} // namespace kqoffice::ai::canvas

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
