/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
#pragma once

#include <sfx2/dllapi.h>

namespace weld
{
class Widget;
}

namespace sfx2
{
/// Floating efficiency pendant (compact today stats + quick actions).
class SFX2_DLLPUBLIC WorkPendantDispatcher
{
public:
    static WorkPendantDispatcher& Get();
    void Show(weld::Widget* pParent = nullptr);
    void Hide();
    bool IsVisible() const;

private:
    WorkPendantDispatcher() = default;
};
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
