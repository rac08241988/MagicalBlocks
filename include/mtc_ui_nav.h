#pragma once
// Keyboard/controller navigation helper for scrollable panels (0.1.4 settings scroll fix).
namespace Rml { class Event; }

namespace mtc::ui {
    // RmlUi's spatial navigation (nav-*: auto) treats every scroll container as a closed
    // navigation boundary: arrow keys can neither enter an `overflow: auto` element from
    // outside nor leave it from inside. Call this from a keydown listener (before RmlUi's
    // default action runs). For arrow keys on an element with nav-<dir>: auto it searches
    // the whole document instead, but only takes over when that search crosses a scroll
    // container boundary (or RmlUi's scoped search would find nothing). Then it focuses the
    // target, scrolls it into view, stops propagation and returns true.
    bool navigate_across_scroll_containers(Rml::Event& event);
}
