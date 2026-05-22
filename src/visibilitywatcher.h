#pragma once

#include <QEvent>
#include <QObject>
#include <QPointer>

class QQuickWindow;

/*
  Tracks whether a QQuickWindow is currently exposed by the windowing
  system — i.e. whether the compositor is asking for frames from it.

  On Wayland (and X11), when our layer surface is fully obscured by an
  opaque window above it, the compositor stops sending frame callbacks
  and Qt sets isExposed() to false. Hooking into that lets the rest of
  the app stop polling /proc and animating Canvas items while nobody is
  looking at the HUD.
*/
class VisibilityWatcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY visibleChanged)
public:
    explicit VisibilityWatcher(QObject* parent = nullptr) : QObject(parent) {}

    void attach(QQuickWindow* window);
    bool visible() const { return m_visible; }

signals:
    void visibleChanged();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void update();

    QPointer<QQuickWindow> m_window;
    bool m_visible = false;
};
