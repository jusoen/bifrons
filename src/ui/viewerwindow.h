#pragma once

#include <QStringList>
#include <QWidget>

namespace dcpp {

class ViewerWidget;

// F3 viewer window. Keys: 1 rendered, 2 text, 3 hex, W wrap, N/P next/previous file, Esc close.
class ViewerWindow : public QWidget
{
    Q_OBJECT

public:
    ViewerWindow(QStringList files, int index, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void showIndex(int index);
    void showPath(const QString &path);

    ViewerWidget *m_viewer = nullptr;
    QStringList m_files;
    int m_index = 0;
};

} // namespace dcpp
