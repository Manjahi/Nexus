#include "MainWindow.hpp"
#include "NotificationBridge.hpp"
#include "Platform.hpp"

#include "nexus/core/version.hpp"
#include "nexus/services/audit_log.hpp"
#include "nexus/services/job_repository.hpp"
#include "nexus/services/notification_repository.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <exception>

namespace {

// Headless integration check: brings up the whole platform, exercises a job run
// and a notification through persistence, and exits. Used for smoke testing
// without a display.
int run_selftest() {
    using namespace nexus;
    nexuspc::desktop::Platform platform;
    auto& ctx = platform.context();

    services::JobRecord record;
    record.module = "platform";
    record.kind = "selftest";
    const auto job_id = ctx.jobs.upsert_job(record);
    const auto run_id = ctx.jobs.start_run(job_id);
    ctx.jobs.update_run_progress(run_id, 0.5, "halfway");
    ctx.jobs.finish_run(run_id, services::JobState::Succeeded);

    ctx.notifications.post("platform", notify::Severity::Success, "Selftest complete");

    const bool job_ok = ctx.jobs.latest_run(job_id).has_value() &&
                        ctx.jobs.latest_run(job_id)->state == services::JobState::Succeeded;
    const bool note_ok = ctx.notifications_repo.recent(1).size() == 1;

    std::printf("selftest: job=%s notifications=%s db=%s\n", job_ok ? "ok" : "FAIL",
                note_ok ? "ok" : "FAIL", platform.database_path().string().c_str());
    return (job_ok && note_ok) ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("NexusPC"));
    QApplication::setOrganizationName(QStringLiteral("NexusPC"));
    QApplication::setApplicationVersion(QString::fromUtf8(nexus::core::version_string));

    if (QApplication::arguments().contains(QStringLiteral("--selftest"))) {
        // Headless: never show a dialog (it would block a non-interactive run).
        try {
            return run_selftest();
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "selftest: FAILED to start: %s\n", ex.what());
            return 1;
        }
    }

    try {
        nexuspc::desktop::Platform platform;
        nexuspc::desktop::NotificationBridge bridge(platform.context().notifications);

        const QString db_path = QString::fromStdWString(platform.database_path().wstring());
        nexuspc::desktop::MainWindow window(platform.context(), db_path, bridge);
        window.show();

        return QApplication::exec();
    } catch (const std::exception& ex) {
        QMessageBox::critical(nullptr, QStringLiteral("NexusPC failed to start"),
                              QString::fromUtf8(ex.what()));
        return 1;
    }
}
