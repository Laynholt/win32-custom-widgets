#include "Test.h"

#include "../src/MenuModel.h"

#include <wcw/Controls.h>

#include <vector>

namespace {

bool SameRect(RECT left, RECT right) {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

} // namespace

int main() {
    const std::vector<wcw::MenuItem> valid{
        {.id = 1, .text = L"Open"},
        {.separator = true},
        {.text = L"More", .children = {{.id = 2, .text = L"Nested"}}},
        {.id = 0, .text = L"Unavailable", .enabled = false},
    };
    const std::vector<wcw::MenuItem> empty;
    const std::vector<wcw::MenuItem> zeroId{{.id = 0, .text = L"Bad"}};
    const std::vector<wcw::MenuItem> largeId{{.id = 65536, .text = L"Bad"}};
    const std::vector<wcw::MenuItem> emptyText{{.enabled = false}};
    const std::vector<wcw::MenuItem> invalidNested{
        {.text = L"Parent", .children = {{.id = 0, .text = L"Bad"}}},
    };
    const std::vector<wcw::MenuItem> ignoredSeparator{
        {.id = 999999, .children = {{.id = 0}}, .separator = true},
    };
    CHECK(wcw::internal::ValidMenuItems(valid));
    CHECK(!wcw::internal::ValidMenuItems(empty));
    CHECK(!wcw::internal::ValidMenuItems(zeroId));
    CHECK(!wcw::internal::ValidMenuItems(largeId));
    CHECK(!wcw::internal::ValidMenuItems(emptyText));
    CHECK(!wcw::internal::ValidMenuItems(invalidNested));
    CHECK(wcw::internal::ValidMenuItems(ignoredSeparator));

    CHECK(wcw::internal::NextMenuIndex(valid, -1, 1) == 0);
    CHECK(wcw::internal::NextMenuIndex(valid, 0, 1) == 2);
    CHECK(wcw::internal::NextMenuIndex(valid, 2, 1) == 0);
    CHECK(wcw::internal::NextMenuIndex(valid, -1, -1) == 2);
    CHECK(wcw::internal::NextMenuIndex(valid, 2, -1) == 0);
    CHECK(wcw::internal::NextMenuIndex(valid, 0, -1) == 2);
    CHECK(wcw::internal::EdgeMenuIndex(valid, false) == 0);
    CHECK(wcw::internal::EdgeMenuIndex(valid, true) == 2);
    const std::vector<wcw::MenuItem> unavailable{
        {.separator = true},
        {.text = L"Disabled", .enabled = false},
    };
    CHECK(wcw::internal::NextMenuIndex(unavailable, -1, 1) == -1);
    CHECK(wcw::internal::EdgeMenuIndex(unavailable, false) == -1);

    const RECT work{0, 0, 800, 600};
    CHECK(SameRect(wcw::internal::PlaceRootMenu({100, 100, 100, 100}, {120, 100}, work),
                   {100, 100, 220, 200}));
    CHECK(SameRect(wcw::internal::PlaceRootMenu({760, 580, 760, 580}, {120, 100}, work),
                   {680, 480, 800, 580}));
    CHECK(SameRect(wcw::internal::PlaceSubmenu({100, 100, 180, 132}, {160, 120}, work),
                   {180, 100, 340, 220}));
    CHECK(SameRect(wcw::internal::PlaceSubmenu({700, 400, 780, 432}, {160, 120}, work),
                   {540, 400, 700, 520}));

    return testFailures;
}
