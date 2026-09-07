#include "MainWindow.hpp"

#include "nexus/core/version.hpp"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("NexusPC"));
    QApplication::setOrganizationName(QStringLiteral("NexusPC"));
    QApplication::setApplicationVersion(QString::fromUtf8(nexus::core::version_string));

    nexuspc::desktop::MainWindow window;
    window.show();

    return QApplication::exec();
}
