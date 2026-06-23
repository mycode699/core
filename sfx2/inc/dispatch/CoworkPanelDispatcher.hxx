/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * V2 W5 Day-1 — Async cowork task manager entry (sfx2 core).
 * Spec: docs/product/v2/w5-async-cowork-spec.md
 */

#pragma once

#include <sfx2/dllapi.h>

#include <rtl/ustring.hxx>

namespace weld { class Widget; }

namespace sfx2
{
using CoworkPanelShowFn = void (*)(weld::Widget* pParent);

class SFX2_DLLPUBLIC CoworkPanelDispatcher
{
public:
    static CoworkPanelDispatcher& Get();

    static void RegisterShowPanelHook(CoworkPanelShowFn fn);

    void ShowPanel(weld::Widget* pParent);

private:
    CoworkPanelDispatcher() = default;
};

} // namespace sfx2

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */