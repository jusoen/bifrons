#pragma once

#include <QDialog>

#include "core/renameengine.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace bifrons {

// Multi-rename (Ctrl+M) with a live preview. The rename runs only when the whole batch is valid.
class MultiRenameDialog : public QDialog
{
    Q_OBJECT

public:
    MultiRenameDialog(QList<RenameItem> items, bool canUndo, QWidget *parent = nullptr);

    // Steps of the rename this dialog performed, empty if none.
    QList<RenameStep> performed() const;
    bool undoRequested() const;

private:
    void updatePreview();
    void rename();
    RenameOptions options() const;
    QPushButton *insertButton(QLineEdit *target);

    QList<RenameItem> m_items;
    RenamePreview m_preview;
    QList<RenameStep> m_performed;
    bool m_undoRequested = false;

    QLineEdit *m_nameMask = nullptr;
    QLineEdit *m_extMask = nullptr;
    QLineEdit *m_find = nullptr;
    QLineEdit *m_replace = nullptr;
    QCheckBox *m_regex = nullptr;
    QCheckBox *m_findCase = nullptr;
    QComboBox *m_case = nullptr;
    QSpinBox *m_counterStart = nullptr;
    QSpinBox *m_counterStep = nullptr;
    QSpinBox *m_counterWidth = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_rename = nullptr;
};

} // namespace bifrons
