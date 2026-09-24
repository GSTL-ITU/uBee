// The bitfield breakdown panel.
//
// The same idea as the terminal version, drawn properly: one renderer driven by
// InstrFormat, so fifteen small field tables cover the whole instruction set --
// six base formats and the nine compressed ones. It answers "how did this line
// become 16 or 32 bits?", and it follows the editor's caret so a line explains
// itself before it has ever been run.
//
// For a compressed instruction it also shows the base instruction the encoding
// stands for, which is the part worth teaching: c.addi is not a new operation,
// it is addi with the operands folded together.
#pragma once

#include <QWidget>

#include "dbg/pipeline.hpp"
#include "dbg/session.hpp"

namespace rv::gui {

class EncodingWidget final : public QWidget {
    Q_OBJECT

public:
    /// The pipeline is optional only so this widget can still be built without
    /// one; when it is there, the panel follows what wrote back rather than the
    /// pc, which by then is four stages further on.
    EncodingWidget(const dbg::DebugSession& session, const dbg::Pipeline* pipeline,
                   QWidget* parent = nullptr);

    /// Show a specific address rather than the pc. Used when the caret moves.
    void set_address(Addr addr);
    void follow_pc();

    QSize sizeHint() const override;
    /// The rendered field descriptions, for the tests.
    QStringList field_summary() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Addr shown_address() const;

    const dbg::DebugSession* session_;
    const dbg::Pipeline* pipeline_;
    Addr address_ = 0;
    bool follow_pc_ = true;
};

}  // namespace rv::gui
