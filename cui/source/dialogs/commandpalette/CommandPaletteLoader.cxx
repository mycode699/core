/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * V2 W2 Day-1b — SfxSlotPool → CommandEntry corpus for Cmd+K palette.
 */

#include <commandpalette/CommandPaletteLoader.hxx>
#include <commandpalette/PinyinHint.hxx>

#include <sal/types.h>
#include <sfx2/msg.hxx>
#include <sfx2/msgpool.hxx>
#include <sfx2/viewfrm.hxx>

namespace cui::commandpalette
{
SAL_DLLPUBLIC_EXPORT std::vector<CommandEntry> CommandPaletteLoader::buildCorpus(SfxViewFrame& rFrame)
{
    std::vector<CommandEntry> out;
    SfxSlotPool& rPool = SfxSlotPool::GetSlotPool(&rFrame);
    const SfxSlotMode nMode(SfxSlotMode::TOOLBOXCONFIG | SfxSlotMode::ACCELCONFIG
                            | SfxSlotMode::MENUCONFIG);

    for (sal_uInt16 nGroup = 0; nGroup < rPool.GetGroupCount(); ++nGroup)
    {
        rPool.SeekGroup(nGroup);
        for (const SfxSlot* pSlot = rPool.FirstSlot(); pSlot; pSlot = rPool.NextSlot())
        {
            if (!(pSlot->GetMode() & nMode))
                continue;

            const OUString& rUno = pSlot->GetUnoName();
            if (rUno.isEmpty() || !rUno.startsWith(".uno:"))
                continue;

            CommandEntry e;
            e.unoCommand = rUno;
            e.labelEn = pSlot->GetCommand();
            PinyinHint::apply(e);
            out.push_back(std::move(e));
        }
    }
    return out;
}

} // namespace cui::commandpalette

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */