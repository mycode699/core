/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * 可圈笔记 · NotebookLM-style workspace dispatcher.
 */
#pragma once

#include <sal/config.h>
#include <rtl/ustring.hxx>
#include <sfx2/dllapi.h>

namespace weld
{
class Window;
}

namespace sfx2
{
class SFX2_DLLPUBLIC KqNotebookDispatcher
{
public:
    static KqNotebookDispatcher& Get();
    void Show(weld::Window* pParent);

    /// If notebook dialog is open, put voice text into chat entry (Spokenly-like).
    /// Returns true if consumed.
    static bool TryAcceptVoiceText(const OUString& rText);

private:
    KqNotebookDispatcher() = default;
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
