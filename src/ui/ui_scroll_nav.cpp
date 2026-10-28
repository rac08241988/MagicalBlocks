// See mtc_ui_nav.h. Mirrors the candidate rules and heuristic of RmlUi's
// ElementDocument::FindNextNavigationElement (MIT), minus the scroll-container boundary.
#include "mtc_ui_nav.h"

#include "RmlUi/Core.h"
#include "RmlUi/Core/Elements/ElementTabSet.h"

#include <algorithm>
#include <climits>

namespace {
    enum class Dir { Up, Down, Left, Right };
    enum class CanFocus { Yes, No, NoAndNoChildren };

    struct Box { Rml::Vector2f min, max; };

    CanFocus can_focus(Rml::Element* e) {
        if (!e->IsVisible()) return CanFocus::NoAndNoChildren;
        const auto& cv = e->GetComputedValues();
        if (cv.focus() == Rml::Style::Focus::None) return CanFocus::NoAndNoChildren;
        if (cv.tab_index() == Rml::Style::TabIndex::Auto) return CanFocus::Yes;
        return CanFocus::No;
    }

    bool is_scroll_container(Rml::Element* e) {
        const auto& cv = e->GetComputedValues();
        return cv.overflow_x() != Rml::Style::Overflow::Visible || cv.overflow_y() != Rml::Style::Overflow::Visible;
    }

    Rml::Element* nearest_scroll_container(Rml::Element* e) {
        for (e = e->GetParentNode(); e; e = e->GetParentNode()) {
            if (is_scroll_container(e)) return e;
        }
        return nullptr;
    }

    Box box_of(Rml::Element* e) {
        Rml::Vector2f p = e->GetAbsoluteOffset(Rml::BoxArea::Border);
        return { p, p + e->GetBox().GetSize(Rml::BoxArea::Border) };
    }

    int heuristic(const Box& src, const Box& dst, Dir dir) {
        constexpr int cross_factor = 10000;
        auto calc = [](int axis, const Box& a, const Box& b) -> int {
            const int main_axis = int(a.min[axis] - b.max[axis]);
            if (main_axis < 0) return INT_MAX;
            const int cross = (axis + 1) % 2;
            const int cross_axis = std::max(0, int(b.min[cross] - a.max[cross])) + std::max(0, int(a.min[cross] - b.max[cross]));
            return main_axis + cross_factor * cross_axis;
        };
        switch (dir) {
            case Dir::Up: return calc(1, src, dst);
            case Dir::Down: return calc(1, dst, src);
            case Dir::Right: return calc(0, dst, src);
            case Dir::Left: return calc(0, src, dst);
        }
        return INT_MAX;
    }

    struct Result { Rml::Element* element = nullptr; int score = INT_MAX; };

    // scoped == true reproduces RmlUi (don't descend into scroll containers).
    void search(Result& best, Rml::Element* e, Dir dir, const Box& src, Rml::Element* exclude, bool scoped) {
        const int n = e->GetNumChildren();
        for (int i = 0; i < n; i++) {
            Rml::Element* c = e->GetChild(i);
            if (c == exclude) continue;
            const CanFocus f = can_focus(c);
            if (f == CanFocus::Yes) {
                const int s = heuristic(src, box_of(c), dir);
                if (s < best.score) { best.element = c; best.score = s; }
            }
            else if (f == CanFocus::NoAndNoChildren || (scoped && is_scroll_container(c))) {
                continue;
            }
            search(best, c, dir, src, exclude, scoped);
        }
    }

    bool is_ancestor(Rml::Element* ancestor, Rml::Element* e) {
        for (; e; e = e->GetParentNode()) if (e == ancestor) return true;
        return false;
    }
}

bool mtc::ui::navigate_across_scroll_containers(Rml::Event& event) {
    if (event.GetId() != Rml::EventId::Keydown) return false;
    const auto key = event.GetParameter<Rml::Input::KeyIdentifier>("key_identifier", Rml::Input::KI_UNKNOWN);
    Dir dir;
    Rml::PropertyId prop;
    switch (key) {
        case Rml::Input::KI_UP: dir = Dir::Up; prop = Rml::PropertyId::NavUp; break;
        case Rml::Input::KI_DOWN: dir = Dir::Down; prop = Rml::PropertyId::NavDown; break;
        case Rml::Input::KI_LEFT: dir = Dir::Left; prop = Rml::PropertyId::NavLeft; break;
        case Rml::Input::KI_RIGHT: dir = Dir::Right; prop = Rml::PropertyId::NavRight; break;
        default: return false;
    }

    Rml::Element* target = event.GetTargetElement();
    if (!target) return false;
    Rml::ElementDocument* doc = target->GetOwnerDocument();
    if (!doc) return false;

    Rml::Element* focus = doc->GetFocusLeafNode();
    while (focus && focus != doc && can_focus(focus) != CanFocus::Yes) focus = focus->GetParentNode();
    if (!focus || focus == doc) return false;

    // Only nav-<dir>: auto (or the matching horizontal/vertical keyword). Explicit #id targets and
    // "none" are left to RmlUi.
    const Rml::Property* p = focus->GetLocalProperty(prop);
    if (!p || p->unit != Rml::Unit::KEYWORD) return false;
    const auto nav = static_cast<Rml::Style::Nav>(p->Get<int>());
    const bool horizontal = dir == Dir::Left || dir == Dir::Right;
    if (nav == Rml::Style::Nav::None) return false;
    if (nav == Rml::Style::Nav::Horizontal && !horizontal) return false;
    if (nav == Rml::Style::Nav::Vertical && horizontal) return false;

    const Box src = box_of(focus);
    Rml::Element* own_container = nearest_scroll_container(focus);

    Result scoped;
    search(scoped, own_container ? own_container : doc, dir, src, focus, true);
    Result global;
    search(global, doc, dir, src, focus, false);
    if (!global.element) return false;

    // Take over when the better target lives in a scroll container we're not in (entering a
    // panel), when RmlUi would find nothing inside our own container (leaving it), or when we're
    // moving inside a scroll container (same target as RmlUi, but see the scrolling note below).
    Rml::Element* next = global.element;
    Rml::Element* target_container = nearest_scroll_container(next);
    const bool entering = target_container && target_container != own_container && !is_ancestor(target_container, focus);
    const bool leaving = own_container && !scoped.element;
    if (!entering && !leaving) {
        if (!own_container || !scoped.element) return false;
        next = scoped.element;
    }

    // Leaving an options list upwards lands on the tab bar: go to the active tab (the one whose
    // panel we're in) rather than whichever tab happens to be geometrically first.
    if (next->GetTagName() == "tab" && next->GetParentNode() && next->GetParentNode()->GetTagName() == "tabs") {
        Rml::Element* tabs = next->GetParentNode();
        if (auto* tabset = rmlui_dynamic_cast<Rml::ElementTabSet*>(tabs->GetParentNode())) {
            const int active = tabset->GetActiveTab();
            if (active >= 0 && active < tabs->GetNumChildren() && can_focus(tabs->GetChild(active)) == CanFocus::Yes) {
                next = tabs->GetChild(active);
            }
        }
    }

    if (!next->Focus(true)) return false;
    // Radio buttons in the option lists are tiny/zero-height anchors next to their labels, so
    // scrolling just them into view leaves the row cut off at the edge. Bring the whole
    // .config-option row into view first (it is at most a few lines tall), then the element.
    for (Rml::Element* e = next->GetParentNode(); e && e != doc; e = e->GetParentNode()) {
        if (is_scroll_container(e)) break;
        if (e->IsClassSet("config-option")) {
            e->ScrollIntoView(Rml::ScrollAlignment::Nearest);
            break;
        }
    }
    next->ScrollIntoView(Rml::ScrollAlignment::Nearest);
    event.StopPropagation();
    return true;
}
