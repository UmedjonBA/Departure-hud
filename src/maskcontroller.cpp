#include "maskcontroller.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QRegion>
#include <QtGlobal>

void MaskController::attach(QQuickWindow* window, QQuickItem* hud) {
    m_window = window;
    m_hud = hud;
    if (!m_window || !m_hud) return;

    auto u = [this]{ update(); };
    connect(m_hud,    &QQuickItem::xChanged,      this, u);
    connect(m_hud,    &QQuickItem::yChanged,      this, u);
    connect(m_hud,    &QQuickItem::widthChanged,  this, u);
    connect(m_hud,    &QQuickItem::heightChanged, this, u);
    connect(m_window, &QQuickWindow::widthChanged,  this, u);
    connect(m_window, &QQuickWindow::heightChanged, this, u);
    update();
}

void MaskController::update() {
    if (!m_window) return;
    if (!m_clickThrough || !m_hud) {
        m_window->setMask(QRegion());
        return;
    }
    const QPointF tl = m_hud->mapToItem(m_window->contentItem(), QPointF(0, 0));
    const int x = qRound(tl.x());
    const int y = qRound(tl.y());
    const int w = qMax(1, qRound(m_hud->width()));
    const int h = qMax(1, qRound(m_hud->height()));
    m_window->setMask(QRegion(x, y, w, h));
}
