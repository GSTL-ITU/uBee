// The peripherals panel.
//
// Lists what is attached, lets devices be added and removed, and renders each
// device's own controls. The controls come from Device::inputs(), so this
// panel can operate a peripheral it has never heard of -- adding a device type
// to the catalogue is enough to make it usable here.
#pragma once

#include <QWidget>

#include <string>

#include "dbg/session.hpp"

class QComboBox;
class QTableWidgetItem;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QVBoxLayout;

namespace rv::gui {

class DevicePanel final : public QWidget {
    Q_OBJECT

public:
    explicit DevicePanel(dbg::DebugSession& session, QWidget* parent = nullptr);

    void refresh();
    void select_row(int row);

    /// Load and apply a device configuration file.
    bool load_config(const QString& path);
    /// What the last load had to say about overlapping addresses, or empty.
    /// Deliberately not a dialog -- see load_config.
    QString last_warning() const { return last_warning_; }
    bool save_config(const QString& path);

signals:
    /// The set of devices changed, so anything showing addresses needs redrawing.
    void devicesChanged();

    /// A control was operated. Distinct from devicesChanged because it does not
    /// invalidate addresses, only values.
    void inputChanged();

private slots:
    void on_add();
    void on_item_changed(QTableWidgetItem* item);
    void on_remove();
    void on_selection_changed();

private:
    /// Redraw the table of what is attached, leaving the controls alone. This
    /// is what a control's own handler calls: rebuilding the controls from
    /// inside one of their signals would delete the widget that is mid-signal.
    void refresh_list();
    void rebuild_controls();
    /// The row a memory block gets above its fields: its depth, and the two
    /// buttons that move its contents in and out as a $readmemh image.
    void add_block_row(std::size_t slot);
    void export_block(std::size_t slot);
    void load_block(std::size_t slot);

public:
    /// Write one block's contents as a $readmemh image, padded to the block's
    /// own depth. Separate from the dialog that asks where, so what comes out
    /// can be checked without one.
    bool write_block(std::size_t slot, const QString& path);

private:
    /// Record the change, redraw the list, and tell the window. The one path
    /// every control takes when it is operated.
    void input_changed();
    /// What the controls currently on screen were built from -- the selected
    /// device and its input values. Rebuilding only when this changes keeps a
    /// control the user is operating from being torn down underneath them.
    std::string controls_signature() const;

    dbg::DebugSession* session_;
    QTableWidget* list_ = nullptr;
    QWidget* controls_ = nullptr;
    QVBoxLayout* controls_layout_ = nullptr;
    QPushButton* remove_ = nullptr;
    std::string controls_built_;
    QString last_warning_;
    /// Set while the panel is writing the table itself, so its own fill is not
    /// mistaken for the user renaming something.
    bool filling_ = false;
};

}  // namespace rv::gui
