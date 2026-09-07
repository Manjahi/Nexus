#pragma once

#include <QObject>

#include "nexus/notify/notification_center.hpp"

namespace nexuspc::desktop {

/// Adapts NotificationCenter's worker-thread observer callback into a Qt signal
/// delivered on the GUI thread.
class NotificationBridge : public QObject {
    Q_OBJECT

public:
    explicit NotificationBridge(nexus::notify::NotificationCenter& center,
                                QObject* parent = nullptr);
    ~NotificationBridge() override;

signals:
    /// Emitted (on this object's thread) whenever a notification is posted.
    void changed();

private:
    nexus::notify::NotificationCenter& center_;
    nexus::notify::NotificationCenter::SubscriptionId subscription_;
};

} // namespace nexuspc::desktop
