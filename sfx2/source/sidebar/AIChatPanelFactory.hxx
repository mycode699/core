/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Stage1: factory prefers shell open; shell may request full panel on idle.
 */
#pragma once

namespace sfx2::sidebar
{
/// Next AIChatPanelFactory create uses full AIChatPanel (not shell).
void RequestFullAIChatPanelNext();
/// true if env KQ_AICHAT_FULL=1 or shell requested full upgrade.
bool PreferFullAIChatPanel();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
