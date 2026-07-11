/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#pragma once

#include <rtl/ustring.hxx>

namespace sfx2::sidebar
{
/// Local OCR for image materials. Never throws; returns empty on failure.
/// Order: macOS Vision (when built) → tesseract CLI if present.
OUString RecognizeImageText(const OUString& rSystemPath);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
