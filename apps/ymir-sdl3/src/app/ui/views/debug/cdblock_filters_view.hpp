#pragma once

namespace ymir::cdblock {
class CDBlock;
}

namespace app::ui {

class CDBlockFiltersView {
public:
    CDBlockFiltersView(ymir::cdblock::CDBlock &cdblock);

    void Display();

private:
    ymir::cdblock::CDBlock &m_cdblock;
};

} // namespace app::ui
