/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-A: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "WriterBodyTextParagraphId.hxx"

#include <doc.hxx>
#include <ndarr.hxx>
#include <ndtxt.hxx>
#include <node.hxx>

namespace sw::inline_actions {

OUString formatBodyTextParagraphId(sal_uInt32 nParagraph)
{
    return OUString(kParagraphIdPrefix) + OUString::number(static_cast<sal_Int64>(nParagraph));
}

std::optional<sal_uInt32> getBodyTextParagraphIndex(const SwDoc& rDoc,
                                                    const SwTextNode& rNode)
{
    sal_uInt32 nParagraph = 0;
    const SwNodes& rNodes = rDoc.GetNodes();
    for (SwNodeOffset nNode(0); nNode < rNodes.Count(); ++nNode)
    {
        const SwTextNode* pTextNode = rNodes[nNode]->GetTextNode();
        if (!pTextNode)
            continue;
        ++nParagraph;
        if (pTextNode == &rNode)
            return nParagraph;
    }
    return std::nullopt;
}

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */