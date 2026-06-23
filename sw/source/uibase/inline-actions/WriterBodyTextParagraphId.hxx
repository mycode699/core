/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project (V2 W4-A: Select-to-Act Writer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <swdllapi.h>
#include <optional>
#include <rtl/ustring.hxx>
#include <sal/types.h>

class SwDoc;
class SwTextNode;

// W3/W4 paragraph_id shape ("swpara-N", 1-based body-text paragraph index).
// Counting matches IntelligentWriterApplyEngine.cxx lcl_*BodyTextParagraph*.

namespace sw::inline_actions {

inline constexpr OUStringLiteral kParagraphIdPrefix = u"swpara-";

SW_DLLPUBLIC OUString formatBodyTextParagraphId(sal_uInt32 nParagraph);

std::optional<sal_uInt32> getBodyTextParagraphIndex(const SwDoc& rDoc,
                                                    const SwTextNode& rNode);

} // namespace sw::inline_actions

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */