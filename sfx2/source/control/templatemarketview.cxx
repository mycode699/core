/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 */

#include <templatemarketview.hxx>

#include <sfx2/templatelocalview.hxx>
#include <startcentertheme.hxx>
#include <templateviewitem.hxx>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <vcl/BitmapWriteAccess.hxx>
#include <vcl/event.hxx>
#include <vcl/font.hxx>
#include <vcl/outdev.hxx>
#include <vcl/svapp.hxx>
#include <vcl/settings.hxx>
#include <vcl/virdev.hxx>
#include <vcl/window.hxx>

namespace
{
// Pages / Numbers template gallery: large paper previews, airy gutters, title under card.
// Multi-section mode lays each category as title + one horizontal row.
constexpr int gnItemPadding = 12;
constexpr tools::Long gnTextHeight = 40; // title under card (Pages ~13–14pt + pad)
constexpr tools::Long gnMaxGutter = 32;
/// Prefer ~4 large cards per horizontal row (Pages-like).
constexpr tools::Long gnIdealCols = 4;
constexpr tools::Long gnMaxCols = 5;
/// How many package thumbnails to open per Idle tick (keeps category switch fluid).
constexpr size_t gnLazyBatch = 4;

struct MarketLayoutTokens
{
    tools::Long nRowGap = 28;
    tools::Long nMinCard = 212;
    tools::Long nMaxCard = 276;
    tools::Long nIdealGutter = 24;
};

MarketLayoutTokens lcl_marketLayout()
{
    // WPS/MS-like density: ~180–280px cards, ~16–18px gutters, hard-cap ~6 columns.
    return MarketLayoutTokens{};
}

std::unordered_map<OUString, Bitmap>& lcl_thumbCache()
{
    static std::unordered_map<OUString, Bitmap> sCache;
    return sCache;
}

/// Fit thumbnail into nEdge×nEdge with white letterbox (no stretch) for crisp page previews.
Bitmap lcl_letterboxToEdge(const Bitmap& rImg, tools::Long nEdge)
{
    if (rImg.IsEmpty() || nEdge <= 0)
        return rImg;
    Bitmap aFit = TemplateLocalView::scaleImg(rImg, nEdge, nEdge);
    const Size aSz = aFit.GetSizePixel();
    if (aSz.Width() <= 0 || aSz.Height() <= 0)
        return aFit;
    if (aSz.Width() == nEdge && aSz.Height() == nEdge)
        return aFit;

    ScopedVclPtrInstance<VirtualDevice> xDev;
    xDev->SetOutputSizePixel(Size(nEdge, nEdge));
    xDev->SetBackground();
    xDev->SetFillColor(COL_WHITE);
    xDev->SetLineColor();
    xDev->DrawRect(tools::Rectangle(Point(0, 0), Size(nEdge, nEdge)));
    const tools::Long nX = (nEdge - aSz.Width()) / 2;
    const tools::Long nY = (nEdge - aSz.Height()) / 2;
    xDev->DrawBitmap(Point(nX, nY), aFit);
    return xDev->GetBitmap(Point(0, 0), Size(nEdge, nEdge));
}

Bitmap lcl_cachedOrFetch(const OUString& rUrl, tools::Long nEdge)
{
    if (rUrl.isEmpty())
        return Bitmap();
    auto& cache = lcl_thumbCache();
    // Cache native package pixels; letterbox per display edge for Hi-DPI sharpness.
    auto it = cache.find(rUrl);
    Bitmap aNative;
    if (it != cache.end())
        aNative = it->second;
    else
    {
        // Fetch at 2× card edge when possible so retina cards stay sharp.
        const tools::Long nFetch = std::max<tools::Long>(nEdge * 2, 256);
        aNative = TemplateLocalView::fetchThumbnail(rUrl, nFetch, nFetch);
        if (!aNative.IsEmpty())
            cache.emplace(rUrl, aNative);
    }
    if (aNative.IsEmpty())
        return Bitmap();
    return lcl_letterboxToEdge(aNative, nEdge);
}
}

TemplateMarketView::TemplateMarketView(std::unique_ptr<weld::ScrolledWindow> xWindow)
    : ThumbnailView(std::move(xWindow))
    , maLazyThumbIdle("TemplateMarketView LazyThumb")
{
    mbAllowMultiSelection = false;
    const MarketLayoutTokens aLayout = lcl_marketLayout();
    // Prefer larger gallery cards; final size is recomputed from the viewport in Resize().
    AbsoluteScreenPixelRectangle aScreen
        = Application::GetScreenPosSizePixel(Application::GetDisplayBuiltInScreen());
    const tools::Long nMid
        = (aLayout.nMinCard + aLayout.nMaxCard) / 2;
    mnItemMaxSize = std::min(aScreen.GetWidth(), aScreen.GetHeight()) > 800
                        ? std::max(nMid, aLayout.nMinCard)
                        : aLayout.nMinCard;
    mnThumbEdge = std::max<tools::Long>(mnItemMaxSize - gnTextHeight + 4, 112);
    // Pin rows; leftover vertical space stays at the bottom instead of inflating gutters.
    mnVItemSpace = aLayout.nRowGap;

    setItemMaxTextLength(18); // Pages shows fuller template titles under cards
    setItemDimensions(mnItemMaxSize, mnThumbEdge, gnTextHeight, gnItemPadding);
    mfHighlightTransparence = 0.75;
    UpdateColors(Application::GetSettings().GetStyleSettings());

    maLazyThumbIdle.SetPriority(TaskPriority::DEFAULT_IDLE);
    maLazyThumbIdle.SetInvokeHandler(LINK(this, TemplateMarketView, LazyThumbIdleHdl));
}

TemplateMarketView::~TemplateMarketView()
{
    maLazyThumbIdle.Stop();
    maPendingThumbs.clear();
}

void TemplateMarketView::recomputeItemMetrics()
{
    const tools::Long nWidth = GetOutputSizePixel().Width();
    if (nWidth <= 0)
        return;

    const MarketLayoutTokens aLayout = lcl_marketLayout();
    mnVItemSpace = aLayout.nRowGap;

    // Prefer ~5 larger cards; hard-cap columns so ultrawide screens do not mint postage stamps.
    tools::Long nBestCard = mnItemMaxSize;
    tools::Long nBestScore = std::numeric_limits<tools::Long>::max();
    for (tools::Long nCard = aLayout.nMinCard; nCard <= aLayout.nMaxCard; nCard += 2)
    {
        const tools::Long nCell = nCard + 2 * gnItemPadding;
        if (nCell <= 0 || nCell > nWidth)
            continue;
        const tools::Long nCols = std::max<tools::Long>(1, nWidth / nCell);
        // Extra width split as gutters between (and beside) columns.
        const tools::Long nGutter = (nWidth - nCols * nCell) / (nCols + 1);
        const tools::Long nScore = std::abs(nGutter - aLayout.nIdealGutter) * 6
                                   + std::abs(nCols - gnIdealCols) * 40
                                   + (nCols > gnMaxCols ? (nCols - gnMaxCols) * 120 : 0)
                                   + (nCols < 3 ? (3 - nCols) * 50 : 0)
                                   + (nGutter > gnMaxGutter ? (nGutter - gnMaxGutter) * 18 : 0)
                                   + (nGutter < 10 ? (10 - nGutter) * 24 : 0);
        if (nScore < nBestScore)
        {
            nBestScore = nScore;
            nBestCard = nCard;
        }
    }

    // Safety: if width still packs more than gnMaxCols, grow card to force the cap.
    {
        const tools::Long nCell = nBestCard + 2 * gnItemPadding;
        const tools::Long nCols = std::max<tools::Long>(1, nWidth / std::max<tools::Long>(nCell, 1));
        if (nCols > gnMaxCols)
        {
            // card ≈ width/cols - padding*2 - gutter share
            const tools::Long nTarget
                = (nWidth / gnMaxCols) - 2 * gnItemPadding - aLayout.nIdealGutter;
            nBestCard = std::clamp(nTarget, aLayout.nMinCard, aLayout.nMaxCard + 40);
        }
    }

    const tools::Long nThumb = std::max<tools::Long>(nBestCard - gnTextHeight + 6, 140);
    if (nBestCard == mnItemMaxSize && nThumb == mnThumbEdge)
        return;

    mnItemMaxSize = nBestCard;
    mnThumbEdge = nThumb;
    setItemDimensions(mnItemMaxSize, mnThumbEdge, gnTextHeight, gnItemPadding);
    scaleItemPreviews();
}

void TemplateMarketView::scaleItemPreviews()
{
    for (auto& pItem : mItemList)
    {
        if (!pItem || pItem->maPreview.IsEmpty())
            continue;
        const Size aSz = pItem->maPreview.GetSizePixel();
        if (aSz.Width() == mnThumbEdge && aSz.Height() == mnThumbEdge)
            continue;
        pItem->maPreview = TemplateLocalView::scaleImg(pItem->maPreview, mnThumbEdge, mnThumbEdge);
    }
}

void TemplateMarketView::Resize()
{
    recomputeItemMetrics();
    ThumbnailView::Resize();
}

void TemplateMarketView::UpdateColors(const StyleSettings& rSettings)
{
    ThumbnailView::UpdateColors(rSettings);
    const auto aTheme = sfx2::sc_theme::tokens();
    // Gallery canvas + title ink follow day/night.
    maFillColor = aTheme.canvas;
    maTextColor = aTheme.textSecondary;
    maHighlightColor = aTheme.selectRing;
    maHighlightTextColor = aTheme.textSecondary;
    mfHighlightTransparence = 0.94;
    (void)rSettings;
}

namespace
{
enum class MarketDocKind
{
    Writer,
    Calc,
    Impress,
    Generic
};

MarketDocKind lcl_docKind(const OUString& rTypeLabel)
{
    if (rTypeLabel.indexOf(u"表") >= 0 || rTypeLabel.indexOf(u"Calc") >= 0
        || rTypeLabel.indexOf(u"XLS") >= 0)
        return MarketDocKind::Calc;
    if (rTypeLabel.indexOf(u"演") >= 0 || rTypeLabel.indexOf(u"PPT") >= 0
        || rTypeLabel.indexOf(u"演示") >= 0)
        return MarketDocKind::Impress;
    if (rTypeLabel.indexOf(u"文") >= 0 || rTypeLabel.indexOf(u"报") >= 0
        || rTypeLabel.indexOf(u"Word") >= 0 || rTypeLabel.indexOf(u"DOC") >= 0)
        return MarketDocKind::Writer;
    return MarketDocKind::Generic;
}

/// Pages-like paper preview when package has no Thumbnails/ — quiet document, not chrome bar.
Bitmap lcl_placeholderThumb(tools::Long nEdge, const OUString& rTypeLabel, const OUString& rTitle)
{
    if (nEdge < 48)
        nEdge = 160;

    const MarketDocKind eKind = lcl_docKind(rTypeLabel);
    Color aAccent = sfx2::sc_theme::brandAccent(); // Writer / Clavue brand blue
    Color aWash(0xE6, 0xF4, 0xFF);
    if (eKind == MarketDocKind::Calc)
    {
        aAccent = Color(0x38, 0x9E, 0x0D);
        aWash = Color(0xF6, 0xFF, 0xED);
    }
    else if (eKind == MarketDocKind::Impress)
    {
        aAccent = Color(0xFA, 0x54, 0x1C);
        aWash = Color(0xFF, 0xF2, 0xE8);
    }

    ScopedVclPtrInstance<VirtualDevice> xDev;
    xDev->SetOutputSizePixel(Size(nEdge, nEdge));
    xDev->SetBackground();
    // Soft canvas (matches card inset).
    xDev->SetFillColor(Color(0xF5, 0xF5, 0xF7));
    xDev->SetLineColor();
    xDev->DrawRect(tools::Rectangle(Point(0, 0), Size(nEdge, nEdge)));

    // Portrait paper with gentle shadow (Pages blank/template feel).
    const tools::Long nPad = std::max<tools::Long>(14, nEdge / 11);
    const tools::Long nPaperW = nEdge - nPad * 2;
    const tools::Long nPaperH = nEdge - nPad * 2 - 4;
    const tools::Long nPaperX = nPad;
    const tools::Long nPaperY = nPad - 2;
    // Shadow
    xDev->SetFillColor(Color(0xE0, 0xE0, 0xE5));
    xDev->DrawRect(
        tools::Rectangle(Point(nPaperX + 2, nPaperY + 3), Size(nPaperW, nPaperH)), 6, 6);
    // Paper
    xDev->SetFillColor(COL_WHITE);
    xDev->SetLineColor(Color(0xE8, 0xE8, 0xED));
    xDev->DrawRect(tools::Rectangle(Point(nPaperX, nPaperY), Size(nPaperW, nPaperH)), 6, 6);

    // Slim top accent (type cue, not a heavy toolbar).
    xDev->SetLineColor();
    xDev->SetFillColor(aAccent);
    xDev->DrawRect(tools::Rectangle(Point(nPaperX, nPaperY), Size(nPaperW, 3)));

    // Soft wash band under accent
    xDev->SetFillColor(aWash);
    xDev->DrawRect(tools::Rectangle(Point(nPaperX + 1, nPaperY + 3), Size(nPaperW - 2, nEdge / 7)));

    OUString aTitle = rTitle;
    if (aTitle.isEmpty())
        aTitle = rTypeLabel;
    vcl::Font aTitleFont(xDev->GetFont());
    aTitleFont.SetWeight(WEIGHT_SEMIBOLD);
    aTitleFont.SetFontHeight(std::max<tools::Long>(12, nEdge / 13));
    xDev->SetFont(aTitleFont);
    xDev->SetTextColor(Color(0x1D, 0x1D, 0x1F));
    const tools::Long nIn = nPaperX + std::max<tools::Long>(10, nPaperW / 12);
    const tools::Long nTitleTop = nPaperY + nEdge / 7 + 8;
    const tools::Long nTitleH = std::max<tools::Long>(32, nEdge / 5);
    xDev->DrawText(tools::Rectangle(nIn, nTitleTop, nPaperX + nPaperW - (nIn - nPaperX),
                                    nTitleTop + nTitleH),
                   aTitle,
                   DrawTextFlags::MultiLine | DrawTextFlags::WordBreak | DrawTextFlags::Top
                       | DrawTextFlags::Left);

    // Body rules
    xDev->SetLineColor(Color(0xEB, 0xEB, 0xF0));
    tools::Long nY = nTitleTop + nTitleH + 6;
    const tools::Long nLineGap = std::max<tools::Long>(11, nEdge / 16);
    const tools::Long nLineEnd = nPaperX + nPaperW - (nIn - nPaperX);
    for (int i = 0; i < 5 && nY < nPaperY + nPaperH - 16; ++i)
    {
        const tools::Long nEnd = (i % 2) ? (nLineEnd - nPaperW / 5) : nLineEnd;
        xDev->DrawLine(Point(nIn, nY), Point(nEnd, nY));
        nY += nLineGap;
    }

    return xDev->GetBitmap(Point(0, 0), Size(nEdge, nEdge));
}
}

OUString TemplateMarketView::getSectionLabelForId(sal_uInt16 nId) const
{
    const auto it = maSectionById.find(nId);
    return it != maSectionById.end() ? it->second : OUString();
}

void TemplateMarketView::setEntries(std::vector<Entry> aEntries)
{
    maLazyThumbIdle.Stop();
    maPendingThumbs.clear();
    maSectionById.clear();
    maSectionPos.clear();
    maSectionMoreRect.clear();
    maSectionMoreLabel.clear();
    maSectionCount.clear();
    maSectionDividerYs.clear();
    mnHoverMoreId = 0;

    recomputeItemMetrics();

    std::vector<std::unique_ptr<ThumbnailViewItem>> aItems;
    aItems.reserve(aEntries.size());
    for (size_t i = 0; i < aEntries.size(); ++i)
    {
        Entry& e = aEntries[i];
        const sal_uInt16 nId = static_cast<sal_uInt16>(i + 1);
        auto pChild = std::make_unique<TemplateViewItem>(*this, nId);
        // ThumbnailView::renameItem defaults to false, so setTitle() would drop the name.
        // Assign maTitle directly (same pattern as TemplateLocalView / RecentDocsViewItem).
        pChild->maTitle = e.title;
        pChild->setPath(e.id);
        pChild->setHelpText(e.helpText.isEmpty() ? e.title : e.helpText);
        if (!e.sectionLabel.isEmpty())
            maSectionById[nId] = e.sectionLabel;

        // Cache hit → letterbox now; miss → title card + Idle fetch (category switch stays snappy).
        if (!e.lazyThumbUrl.isEmpty())
        {
            auto& cache = lcl_thumbCache();
            auto it = cache.find(e.lazyThumbUrl);
            if (it != cache.end() && !it->second.IsEmpty())
                pChild->maPreview = lcl_letterboxToEdge(it->second, mnThumbEdge);
            else
            {
                Bitmap aPh = e.thumbnail.IsEmpty()
                                 ? lcl_placeholderThumb(mnThumbEdge, e.helpText, e.title)
                                 : lcl_letterboxToEdge(e.thumbnail, mnThumbEdge);
                pChild->maPreview = aPh;
                maPendingThumbs.emplace_back(e.id, e.lazyThumbUrl);
            }
        }
        else if (!e.thumbnail.IsEmpty())
            pChild->maPreview = lcl_letterboxToEdge(e.thumbnail, mnThumbEdge);
        else
            pChild->maPreview = lcl_placeholderThumb(mnThumbEdge, e.helpText, e.title);

        aItems.push_back(std::move(pChild));
    }
    updateItems(std::move(aItems));

    if (!maPendingThumbs.empty())
        maLazyThumbIdle.Start();
}

void TemplateMarketView::CalculateItemPositions(bool bScrollBarUsed)
{
    if (!mnItemHeight || !mnItemWidth)
        return;

    const Size aWinSize = GetOutputSizePixel();
    const size_t nItemCount = mFilteredItemList.size();
    maSectionPos.clear();
    maSectionMoreRect.clear();
    maSectionMoreLabel.clear();
    maSectionCount.clear();
    maSectionDividerYs.clear();

    if (nItemCount == 0)
    {
        ThumbnailView::CalculateItemPositions(bScrollBarUsed);
        return;
    }

    // If no sections, use stock grid.
    bool bAnySection = false;
    for (const auto& p : mFilteredItemList)
    {
        if (p && !getSectionLabelForId(p->mnId).isEmpty())
        {
            bAnySection = true;
            break;
        }
    }
    if (!bAnySection)
    {
        ThumbnailView::CalculateItemPositions(bScrollBarUsed);
        return;
    }

    // —— Pages/Numbers style: each section = title row + ONE horizontal card row ——
    // Only place cards that fit in the viewport width; overflow → title-row "更多 ›".
    const tools::Long nScrBarW = mbAllowVScrollBar ? mxScrolledWindow->get_scroll_thickness() : 0;
    const tools::Long nPadX = 22;
    const tools::Long nGap = 22;
    const tools::Long nSectionH = gnSectionHeaderH;
    const tools::Long nRowGap = 40; // airy section rhythm (Pages gallery)
    const tools::Long nStartX = nPadX;
    const tools::Long nViewInnerW
        = std::max<tools::Long>(280, aWinSize.Width() - nScrBarW - nPadX * 2);
    // How many full cards fit in one horizontal row (Pages shows ~4–5).
    const tools::Long nFitCols = std::max<tools::Long>(
        2, (nViewInnerW + nGap) / std::max<tools::Long>(1, mnItemWidth + nGap));
    mnCols = static_cast<sal_uInt16>(std::min<tools::Long>(nFitCols, 6));

    // Build section groups: consecutive items; section starts where label is set.
    struct SecGroup
    {
        OUString label;
        sal_uInt16 headerId = 0;
        std::vector<size_t> indices;
    };
    std::vector<SecGroup> groups;
    for (size_t i = 0; i < nItemCount; ++i)
    {
        ThumbnailViewItem* pItem = mFilteredItemList[i];
        if (!pItem)
            continue;
        const OUString aSec = getSectionLabelForId(pItem->mnId);
        if (!aSec.isEmpty() || groups.empty())
        {
            SecGroup g;
            g.label = aSec.isEmpty() ? u"模板"_ustr : aSec;
            g.headerId = pItem->mnId;
            g.indices.push_back(i);
            groups.push_back(std::move(g));
        }
        else
            groups.back().indices.push_back(i);
    }

    // Content size: each group is header + single row (at most mnCols cards painted).
    tools::Long nContentH
        = 12 + static_cast<tools::Long>(groups.size()) * (nSectionH + mnItemHeight + nRowGap);
    nContentH = std::max<tools::Long>(nContentH, aWinSize.Height());

    tools::Long nScrollY = 0;
    if (bScrollBarUsed)
        nScrollY = static_cast<tools::Long>(mxScrolledWindow->vadjustment_get_value());

    mbHasVisibleItems = true;
    mbScroll = nContentH > aWinSize.Height() + 2;
    mnVisLines = static_cast<sal_uInt16>(
        std::max<tools::Long>(1, aWinSize.Height() / std::max<tools::Long>(1, mnItemHeight + nSectionH)));
    mnLines = static_cast<sal_uInt16>(
        std::min<tools::Long>(0x7fff, groups.empty() ? 1 : static_cast<tools::Long>(groups.size())));

    mxScrolledWindow->vadjustment_set_upper(std::max<tools::Long>(8, nContentH));
    mxScrolledWindow->vadjustment_set_page_size(aWinSize.Height());
    mxScrolledWindow->vadjustment_set_page_increment(
        std::max<tools::Long>(mnItemHeight + nSectionH, aWinSize.Height() / 3));
    if (!bScrollBarUsed)
        mxScrolledWindow->vadjustment_set_value(0);
    if (mbAllowVScrollBar)
        mxScrolledWindow->set_vpolicy(mbScroll ? VclPolicyType::ALWAYS : VclPolicyType::NEVER);

    // Hide all first, then place visible.
    for (size_t i = 0; i < nItemCount; ++i)
    {
        if (mFilteredItemList[i] && mFilteredItemList[i]->isVisible())
        {
            mFilteredItemList[i]->show(false);
            maItemStateHdl.Call(mFilteredItemList[i]);
        }
    }

    tools::Long y = 18 - nScrollY;
    for (size_t gi = 0; gi < groups.size(); ++gi)
    {
        const auto& g = groups[gi];
        const tools::Long nTitleY = y + 10;
        maSectionPos[g.headerId] = Point(nStartX, nTitleY);
        maSectionCount[g.headerId] = static_cast<sal_uInt16>(
            std::min<size_t>(g.indices.size(), 999));

        // Show "更多 ›" when the section has more cards than fit, or when
        // the label is a real category the left rail can open (not 最近使用 alone).
        const size_t nCap = static_cast<size_t>(mnCols);
        const bool bHasOverflow = g.indices.size() > nCap;
        const bool bCategoryJump
            = !g.label.isEmpty() && g.label != u"最近使用"_ustr && g.label != u"模板"_ustr;
        if (bHasOverflow || (bCategoryJump && g.indices.size() >= 3))
        {
            // Right-aligned on title baseline (Apple Pages "See All" pattern).
            constexpr tools::Long nMoreW = 72;
            constexpr tools::Long nMoreH = 24;
            const tools::Long nMoreX = aWinSize.Width() - nScrBarW - nPadX - nMoreW;
            maSectionMoreRect[g.headerId]
                = tools::Rectangle(Point(nMoreX, nTitleY - 3), Size(nMoreW, nMoreH));
            maSectionMoreLabel[g.headerId] = u"更多 ›"_ustr;
        }

        y += nSectionH;
        tools::Long x = nStartX;
        const tools::Long nRowTop = y;
        size_t nPlaced = 0;
        for (size_t idx : g.indices)
        {
            ThumbnailViewItem* pItem = mFilteredItemList[idx];
            if (!pItem)
                continue;

            // One horizontal row: only place cards that fully fit.
            if (nPlaced >= nCap)
            {
                if (pItem->isVisible())
                {
                    pItem->show(false);
                    maItemStateHdl.Call(pItem);
                }
                continue;
            }

            const bool bInView
                = (nRowTop + mnItemHeight >= 0) && (nRowTop < aWinSize.Height() + mnItemHeight);
            if (bInView != pItem->isVisible())
            {
                pItem->show(bInView);
                maItemStateHdl.Call(pItem);
            }
            if (pItem->isVisible())
            {
                pItem->setDrawArea(
                    tools::Rectangle(Point(x, nRowTop), Size(mnItemWidth, mnItemHeight)));
                pItem->calculateItemsPosition(mnThumbnailHeight, mnItemPadding,
                                              mpItemAttrs->nMaxTextLength, mpItemAttrs.get());
            }
            x += mnItemWidth + nGap;
            ++nPlaced;
        }

        y = nRowTop + mnItemHeight + nRowGap;
        // Hairline between sections (not after the last).
        if (gi + 1 < groups.size())
            maSectionDividerYs.push_back(y - nRowGap / 2);
    }
}

void TemplateMarketView::Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect)
{
    ThumbnailView::Paint(rRenderContext, rRect);
    if (maSectionPos.empty())
        return;

    auto popIt = rRenderContext.ScopedPush(vcl::PushFlags::FONT | vcl::PushFlags::TEXTCOLOR
                                           | vcl::PushFlags::FILLCOLOR
                                           | vcl::PushFlags::LINECOLOR);

    const tools::Long nViewH = GetOutputSizePixel().Height();
    const tools::Long nViewW = GetOutputSizePixel().Width();
    const auto aTheme = sfx2::sc_theme::tokens();

    // Soft hairlines between section groups (Pages quiet separators).
    if (!maSectionDividerYs.empty())
    {
        rRenderContext.SetLineColor(aTheme.divider);
        rRenderContext.SetFillColor();
        for (tools::Long nY : maSectionDividerYs)
        {
            if (nY < -2 || nY > nViewH + 2)
                continue;
            rRenderContext.DrawLine(Point(22, nY), Point(std::max<tools::Long>(40, nViewW - 28), nY));
        }
    }

    // Section titles — Pages gallery (~15–16pt semibold primary label).
    vcl::Font aFont(rRenderContext.GetFont());
    aFont.SetWeight(WEIGHT_SEMIBOLD);
    aFont.SetFontHeight(std::max<tools::Long>(15, aFont.GetFontHeight() + 2));
    rRenderContext.SetFont(aFont);
    rRenderContext.SetTextColor(aTheme.textPrimary);
    rRenderContext.SetLineColor();
    rRenderContext.SetFillColor();

    for (const auto& kv : maSectionPos)
    {
        const OUString aLabel = getSectionLabelForId(kv.first);
        if (aLabel.isEmpty())
            continue;
        const Point& pt = kv.second;
        if (pt.Y() < -28 || pt.Y() > nViewH + 28)
            continue;
        rRenderContext.DrawText(pt, aLabel);

        // Subtle count caption after title (secondary gray).
        const auto itCnt = maSectionCount.find(kv.first);
        if (itCnt != maSectionCount.end() && itCnt->second > 0)
        {
            const tools::Long nLabelW = rRenderContext.GetTextWidth(aLabel);
            vcl::Font aCnt(aFont);
            aCnt.SetWeight(WEIGHT_NORMAL);
            aCnt.SetFontHeight(std::max<tools::Long>(12, aFont.GetFontHeight() - 3));
            rRenderContext.SetFont(aCnt);
            rRenderContext.SetTextColor(aTheme.textTertiary);
            const OUString aCntText = u"  "_ustr + OUString::number(itCnt->second);
            rRenderContext.DrawText(Point(pt.X() + nLabelW, pt.Y() + 2), aCntText);
            rRenderContext.SetFont(aFont);
            rRenderContext.SetTextColor(aTheme.textPrimary);
        }
    }

    // Title-row "更多 ›" — system blue, medium weight (Pages See All); darker on hover.
    if (!maSectionMoreRect.empty())
    {
        vcl::Font aMore(rRenderContext.GetFont());
        aMore.SetWeight(WEIGHT_MEDIUM);
        aMore.SetFontHeight(std::max<tools::Long>(13, aMore.GetFontHeight() - 1));
        rRenderContext.SetFont(aMore);
        Color aMoreInk = aTheme.moreLink;
        Color aMoreInkHover = aTheme.bDark ? Color(0x91, 0xCA, 0xFF) : Color(0x09, 0x58, 0xD9);
        for (const auto& kv : maSectionMoreRect)
        {
            const auto it = maSectionMoreLabel.find(kv.first);
            if (it == maSectionMoreLabel.end())
                continue;
            const tools::Rectangle& r = kv.second;
            if (r.Bottom() < 0 || r.Top() > nViewH)
                continue;
            const bool bHover = (kv.first == mnHoverMoreId);
            rRenderContext.SetTextColor(bHover ? aMoreInkHover : aMoreInk);
            if (bHover)
            {
                // Quiet pill under hover (not a heavy button).
                rRenderContext.SetFillColor(aTheme.moreHover);
                rRenderContext.SetLineColor();
                rRenderContext.DrawRect(r, 8, 8);
            }
            const tools::Long nTw = rRenderContext.GetTextWidth(it->second);
            const tools::Long nTh = rRenderContext.GetTextHeight();
            const Point pt(r.Right() - nTw - 4, r.Top() + (r.GetHeight() - nTh) / 2);
            rRenderContext.DrawText(pt, it->second);
        }
    }
    (void)rRect;
}

sal_uInt16 TemplateMarketView::hitSectionMoreId(const Point& rPos) const
{
    for (const auto& kv : maSectionMoreRect)
    {
        if (kv.second.Contains(rPos))
            return kv.first;
    }
    return 0;
}

OUString TemplateMarketView::hitSectionMore(const Point& rPos) const
{
    for (const auto& kv : maSectionMoreRect)
    {
        if (!kv.second.Contains(rPos))
            continue;
        const OUString aLabel = getSectionLabelForId(kv.first);
        if (!aLabel.isEmpty())
            return aLabel;
        const auto it = maSectionMoreLabel.find(kv.first);
        if (it != maSectionMoreLabel.end())
            return it->second;
    }
    return OUString();
}

IMPL_LINK_NOARG(TemplateMarketView, LazyThumbIdleHdl, Timer*, void)
{
    size_t nDone = 0;
    while (!maPendingThumbs.empty() && nDone < gnLazyBatch)
    {
        const OUString aId = maPendingThumbs.front().first;
        const OUString aUrl = maPendingThumbs.front().second;
        maPendingThumbs.erase(maPendingThumbs.begin());
        ++nDone;

        Bitmap aBmp = lcl_cachedOrFetch(aUrl, mnThumbEdge);
        // HF packages without Thumbnails/ keep the title card painted at setEntries.
        if (aBmp.IsEmpty())
            continue;
        if (aBmp.GetSizePixel().Width() < 24 || aBmp.GetSizePixel().Height() < 24)
            continue;

        for (auto& pItem : mItemList)
        {
            auto* pView = dynamic_cast<TemplateViewItem*>(pItem.get());
            if (!pView || pView->getPath() != aId)
                continue;
            pView->maPreview = aBmp;
            DrawItem(pView);
            break;
        }
    }

    if (!maPendingThumbs.empty())
        maLazyThumbIdle.Start();
}

OUString TemplateMarketView::getSelectedId() const
{
    for (size_t i = 0; i < mItemList.size(); ++i)
    {
        ThumbnailViewItem* pItem = mItemList[i].get();
        if (pItem && pItem->mbSelected)
        {
            if (auto* pView = dynamic_cast<TemplateViewItem*>(pItem))
                return pView->getPath();
        }
    }
    return OUString();
}

void TemplateMarketView::activateItem(ThumbnailViewItem* pItem)
{
    auto* pView = dynamic_cast<TemplateViewItem*>(pItem);
    if (!pView)
        return;
    const OUString& id = pView->getPath();
    if (!id.isEmpty())
        maActivateHdl.Call(id);
}

bool TemplateMarketView::MouseMove(const MouseEvent& rMEvt)
{
    const sal_uInt16 nHit = hitSectionMoreId(rMEvt.GetPosPixel());
    if (nHit != mnHoverMoreId)
    {
        mnHoverMoreId = nHit;
        Invalidate();
    }
    return ThumbnailView::MouseMove(rMEvt);
}

bool TemplateMarketView::MouseButtonDown(const MouseEvent& rMEvt)
{
    GrabFocus();
    if (rMEvt.IsLeft())
    {
        // Title-row "更多 ›" → jump to that category (Pages See All).
        const OUString aMoreSec = hitSectionMore(rMEvt.GetPosPixel());
        if (!aMoreSec.isEmpty())
        {
            if (maSectionMoreHdl.IsSet())
                maSectionMoreHdl.Call(aMoreSec);
            return true;
        }

        size_t nPos = ImplGetItem(rMEvt.GetPosPixel());
        ThumbnailViewItem* pItem = ImplGetItem(nPos);
        if (pItem)
        {
            // Mature gallery UX (MS/WPS): single-click selects; double-click creates.
            SelectItem(pItem->mnId);
            if (rMEvt.GetClicks() >= 2)
                activateItem(pItem);
            return true;
        }
    }
    return ThumbnailView::MouseButtonDown(rMEvt);
}

void TemplateMarketView::OnItemDblClicked(ThumbnailViewItem* pItem) { activateItem(pItem); }

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
