#include "visibilitywatcher.h"

#include <QQuickWindow>

void VisibilityWatcher::attach(QQuickWindow* window) {
    if (m_window) m_window->removeEventFilter(this);
    m_window = window;
    if (!m_window) return;
    m_window->installEventFilter(this);
    update();
}

bool VisibilityWatcher::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_window) {
        switch (event->type()) {
            case QEvent::Expose:
            case QEvent::Show:
            case QEvent::Hide:
            case QEvent::WindowStateChange:
                update();
                break;
            default:
                break;
        }
    }
    return QObject::eventFilter(obj, event);
}

void VisibilityWatcher::update() {
    const bool v = m_window && m_window->isExposed();
    if (v != m_visible) {
        m_visible = v;
        emit visibleChanged();
    }
}
