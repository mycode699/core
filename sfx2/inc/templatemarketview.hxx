/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the 可圈office project.
 *
 * Start Center template market card grid (ThumbnailView).
 */

#pragma once

#include <sfx2/thumbnailview.hxx>
#include <vcl/bitmap.hxx>
#include <vcl/idle.hxx>

#include <map>
#include <utility>
#include <vector>

class TemplateViewItem;

/// Card grid for the Start Center「模板中心」market (catalog + HF package).
class TemplateMarketView final : public ThumbnailView
{
public:
    explicit TemplateMarketView(std::unique_ptr<weld::ScrolledWindow> xWindow);
    virtual ~TemplateMarketView() override;

    struct Entry
    {
        OUString id; ///< path:… / ai:… / hint:… / tool:…
        OUString title;
        OUString helpText;
        Bitmap thumbnail; ///< Placeholder or already-resolved preview.
        /// When non-empty, real package thumbnail is loaded on Idle (category switch stays snappy).
        OUString lazyThumbUrl;
        /// Pages-style section header painted above this card (first of group only).
        OUString sectionLabel;
    };

    /// Replace all cards (invalidates prior item pointers).
    void setEntries(std::vector<Entry> aEntries);

    bool empty() const { return mItemList.empty(); }

    /// Selected card market id, or empty.
    OUString getSelectedId() const;

    void setActivateHdl(const Link<const OUString&, void>& rLink) { maActivateHdl = rLink; }
    /// Pages "See All" / 更多 › — section label is the argument (e.g. 推荐).
    void setSectionMoreHdl(const Link<const OUString&, void>& rLink) { maSectionMoreHdl = rLink; }

    /// Section title for item id (empty if none).
    OUString getSectionLabelForId(sal_uInt16 nId) const;

protected:
    virtual void UpdateColors(const StyleSettings& rSettings) override;
    virtual void Resize() override;
    /// Pages-like: section title + single horizontal card row per group.
    void CalculateItemPositions(bool bScrollBarUsed = false) override;
    void Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect) override;

private:
    virtual bool MouseButtonDown(const MouseEvent& rMEvt) override;
    virtual bool MouseMove(const MouseEvent& rMEvt) override;
    virtual void OnItemDblClicked(ThumbnailViewItem* pItem) override;

    void activateItem(ThumbnailViewItem* pItem);
    /// Hit-test section "更多 ›" affordance; returns section label or empty.
    OUString hitSectionMore(const Point& rPos) const;
    /// Hit-test returns section header id for "更多 ›", or 0.
    sal_uInt16 hitSectionMoreId(const Point& rPos) const;
    /// Pick card size / gutters for the current viewport (dense at wide + narrow).
    void recomputeItemMetrics();
    /// Scale previews to the active thumbnail box so default icons fill the card.
    void scaleItemPreviews();
    DECL_LINK(LazyThumbIdleHdl, Timer*, void);

    tools::Long mnItemMaxSize = 172;
    tools::Long mnThumbEdge = 146;
    static constexpr tools::Long gnSectionHeaderH = 44;
    Link<const OUString&, void> maActivateHdl;
    Link<const OUString&, void> maSectionMoreHdl;

    Idle maLazyThumbIdle;
    /// Pending (market id, file URL) pairs for Idle thumbnail fetch.
    std::vector<std::pair<OUString, OUString>> maPendingThumbs;
    /// First-of-group section labels keyed by item id.
    std::map<sal_uInt16, OUString> maSectionById;
    /// Paint positions for section titles (id → baseline top-left).
    std::map<sal_uInt16, Point> maSectionPos;
    /// Optional "更多 ›" next to section title (id → draw rect for hit-test).
    std::map<sal_uInt16, tools::Rectangle> maSectionMoreRect;
    std::map<sal_uInt16, OUString> maSectionMoreLabel;
    /// Section card counts (for subtle "推荐  6" secondary caption).
    std::map<sal_uInt16, sal_uInt16> maSectionCount;
    /// Y of hairline under each section (except last); empty when no multi-section.
    std::vector<tools::Long> maSectionDividerYs;
    /// Hovered "更多 ›" section header id (0 = none).
    sal_uInt16 mnHoverMoreId = 0;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
