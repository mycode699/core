/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#pragma once

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{
/// Deeper local text extraction for notebook materials
/// (PDFium + ODT/DOCX ZIP/XML + image OCR).
/// Returns empty on failure; never throws.
OUString EnrichMaterialText(const OUString& rSystemPath, const OUString& rKind);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
