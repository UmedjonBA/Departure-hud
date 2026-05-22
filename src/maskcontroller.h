#pragma once

#include <QObject>
#include <QPointer>

class QQuickItem;
class QQuickWindow;

class MaskController : public QObject {
    Q_OBJECT
public:
    explicit MaskController(QObject* parent = nullptr) : QObject(parent) {}

    void setClickThrough(bool b) { m_clickThrough = b; update(); }
    void attach(QQuickWindow* window, QQuickItem* hud);

private:
    void update();

    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem>   m_hud;
    bool m_clickThrough = true;
};
