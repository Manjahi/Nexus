#pragma once

#include <QMainWindow>

class QListWidget;
class QStackedWidget;

namespace nexuspc::desktop {

/// Application shell: a left-hand navigation list bound to a stack of module
/// pages. Pages are placeholders until their milestones land.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildNavigation();
    void addPage(const QString& name, const QString& blurb);

    QListWidget* nav_{nullptr};
    QStackedWidget* pages_{nullptr};
};

} // namespace nexuspc::desktop
