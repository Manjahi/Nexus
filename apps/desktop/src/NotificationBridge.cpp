#include "NotificationBridge.hpp"

namespace nexuspc::desktop {

NotificationBridge::NotificationBridge(nexus::notify::NotificationCenter& center, QObject* parent)
    : QObject(parent), center_(center) {
    subscription_ = center_.subscribe([this](const nexus::notify::Notification&) {
        // Hop to this object's (GUI) thread before touching Qt.
        QMetaObject::invokeMethod(this, [this] { emit changed(); }, Qt::QueuedConnection);
    });
}

NotificationBridge::~NotificationBridge() {
    center_.unsubscribe(subscription_);
}

} // namespace nexuspc::desktop
