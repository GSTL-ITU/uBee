// The pipeline panel.
//
// Two pictures of the same thing. On top, the five stages as they stand right
// now: which instruction is in each, and which one is in EX, where the machine
// actually changes. Underneath, the diagram every textbook draws -- one row per
// instruction, one column per cycle -- because that is the one that answers
// "why did those four instructions take seven cycles?".
//
// Each stage keeps its own colour, so IF is always the same blue and the shape
// of the diagram is readable without reading any of the words in it.
#pragma once

#include <QWidget>

#include "dbg/pipeline.hpp"
#include "dbg/session.hpp"

namespace rv::gui {

class PipelineWidget final : public QWidget {
    Q_OBJECT

public:
    PipelineWidget(const dbg::Pipeline& pipeline, const dbg::DebugSession& session,
                   QWidget* parent = nullptr);

    QSize sizeHint() const override;

    /// What the strip says, for the tests: "IF=addi@0004" per stage, or
    /// "MEM=-" where there is a bubble.
    QStringList stage_summary() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int draw_strip(QPainter& painter, int y);
    void draw_diagram(QPainter& painter, int y);

    const dbg::Pipeline* pipeline_;
    const dbg::DebugSession* session_;
};

}  // namespace rv::gui
