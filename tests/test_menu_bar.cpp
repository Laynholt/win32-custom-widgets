#include "../src/MenuModel.h"
#include "Test.h"

#include <wcw/Controls.h>

#include <vector>

int main() {
    using wcw::MenuItem;
    using wcw::internal::ParseMenuLabel;
    using wcw::internal::ValidMenuBarItems;

    const auto file = ParseMenuLabel(L"&File");
    CHECK(file.text == L"File");
    CHECK(file.mnemonic == L'f');

    const auto escaped = ParseMenuLabel(L"Save && Close");
    CHECK(escaped.text == L"Save & Close");
    CHECK(escaped.mnemonic == 0);

    const auto trailing = ParseMenuLabel(L"Help&");
    CHECK(trailing.text == L"Help&");
    CHECK(trailing.mnemonic == 0);

    const std::vector<MenuItem> valid{
        {.text = L"&File", .children = {{.id = 101, .text = L"Open"}}},
        {.id = 102, .text = L"&Help"},
    };
    CHECK(ValidMenuBarItems(valid));
    const std::vector<MenuItem> empty;
    const std::vector<MenuItem> separator{{.separator = true}};
    const std::vector<MenuItem> emptyLabel{{.id = 1, .text = L"&"}};
    CHECK(!ValidMenuBarItems(empty));
    CHECK(!ValidMenuBarItems(separator));
    CHECK(!ValidMenuBarItems(emptyLabel));

    const std::vector<MenuItem> navigation{
        {.text = L"Disabled", .children = {{.id = 1, .text = L"One"}}, .enabled = false},
        {.text = L"File", .children = {{.id = 2, .text = L"Two"}}},
        {.text = L"Edit", .children = {{.id = 3, .text = L"Three"}}},
    };
    CHECK(wcw::internal::NextMenuIndex(navigation, -1, 1) == 1);
    CHECK(wcw::internal::NextMenuIndex(navigation, 1, -1) == 2);

    wcw::MenuBarOptions options;
    options.items = valid;
    return testFailures ? 1 : 0;
}
