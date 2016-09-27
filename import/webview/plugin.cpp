/****************************************************************************
**
** Copyright (C) 2016 Jolla Ltd.
** Contact: Chris Adams <chris.adams@jollamobile.com>
**
****************************************************************************/

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "plugin.h"
#include "webengine.h"
#include "webenginesettings.h"

#include <QtCore/QTimer>
#include <QtCore/QStandardPaths>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QStyleHints>
#include <QtGui/QMouseEvent>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlContext>
#include <QtQuick/QQuickWindow>
#include <private/qquickwindow_p.h>

#define SAILFISHOS_WEBVIEW_MOZILLA_COMPONENTS_PATH QLatin1String("/usr/lib/mozembedlite/")

namespace SailfishOS {

namespace WebView {

void SailfishOSWebViewPlugin::registerTypes(const char *uri)
{
    Q_ASSERT(uri == QLatin1String("Sailfish.WebView"));
    qmlRegisterType<SailfishOS::WebView::RawWebView>("Sailfish.WebView", 1, 0, "RawWebView");
}

void SailfishOSWebViewPlugin::initializeEngine(QQmlEngine *engine, const char *uri)
{
    Q_ASSERT(uri == QLatin1String("Sailfish.WebView"));

    AppTranslator *engineeringEnglish = new AppTranslator(engine);
    AppTranslator *translator = new AppTranslator(engine);
    engineeringEnglish->load("sailfish_components_webview_qt5_eng_en", "/usr/share/translations");
    translator->load(QLocale(), "sailfish_components_webview_qt5", "-", "/usr/share/translations");

    SailfishOS::WebEngine::initialize(QStandardPaths::writableLocation(QStandardPaths::CacheLocation),
                                      QLatin1String("Mozilla/5.0 (Maemo; Linux; U; Sailfish OS 2.0 (like Android 4.4); Mobile; rv:38.0) Gecko/38.0 Firefox/38.0 SailfishBrowser/1.0"));

    SailfishOS::WebEngine *webEngine = SailfishOS::WebEngine::instance();

    SailfishOS::WebEngineSettings::initialize();
    SailfishOS::WebEngineSettings *engineSettings = SailfishOS::WebEngineSettings::instance();

    // For some yet unknown reason QmlMozView crashes when
    // flicking quickly if progressive-paint is enabled.
    engineSettings->setPreference("layers.progressive-paint", QVariant::fromValue<bool>(false));
    // Disable low-precision-buffer so that background underdraw works
    // correctly.
    engineSettings->setPreference("layers.low-precision-buffer", QVariant::fromValue<bool>(false));

    // Don't expose any protocol handlers by default and don't warn about those.
    engineSettings->setPreference(QStringLiteral("network.protocol-handler.external-default"), false);
    engineSettings->setPreference(QStringLiteral("network.protocol-handler.expose-all"), false);
    engineSettings->setPreference(QStringLiteral("network.protocol-handler.warn-external-default"), false);

    // TODO : Stop embedding after lastWindow is destroyed.
    connect(engine, SIGNAL(destroyed()), webEngine, SLOT(stopEmbedding()));
}

RawWebView::RawWebView(QQuickItem *parent)
    : QuickMozView(parent)
    , m_flickable(0)
    , m_startPos(-1.0, -1.0)
    , m_bottomMargin(0)
{
    setFiltersChildMouseEvents(true);
}

RawWebView::~RawWebView()
{
}

QQuickItem *RawWebView::flickable() const
{
    return m_flickable;
}

void RawWebView::setFlickable(QQuickItem *flickable)
{
    // TODO: currently unneeded
    if (m_flickable != flickable) {
        m_flickable = flickable;
        if (m_flickable) {
        }
        emit flickableChanged();
    }
}

qreal RawWebView::bottomMargin() const
{
    return m_bottomMargin;
}

void RawWebView::setBottomMargin(qreal margin)
{
    if (margin != m_bottomMargin) {
        m_bottomMargin = margin;
        QMargins margins;
        margins.setBottom(m_bottomMargin);
        setMargins(margins);
        emit bottomMarginChanged();
    }
}

int RawWebView::findTouch(int id) const
{
    auto it = std::find_if(m_touchPoints.begin(), m_touchPoints.end(), [id](const QTouchEvent::TouchPoint& tp) { return tp.id() == id; });
    return it != m_touchPoints.end() ? it - m_touchPoints.begin() : -1;
}

void RawWebView::mouseUngrabEvent()
{
    qDebug() << "UNGRAB!!";
}

void RawWebView::touchEvent(QTouchEvent *event)
{
    qDebug() << "TOUCH" << event;
    handleTouchEvent(event);
    event->setAccepted(true);
}

void RawWebView::handleTouchEvent(QTouchEvent *event)
{
    if (event->type() == QEvent::TouchCancel) {
        setKeepMouseGrab(false);
        setKeepTouchGrab(false);
        QuickMozView::touchEvent(event);
        m_touchPoints.clear();
        return;
    }

    QQuickWindow *win = window();
    QQuickItem *grabber = win ? win->mouseGrabberItem() : 0;

    if (grabber && grabber != this && grabber->keepMouseGrab()) {
        if (!m_touchPoints.isEmpty()) {
            QTouchEvent localEvent(QEvent::TouchCancel);
            localEvent.setTouchPoints(m_touchPoints);
            QuickMozView::touchEvent(&localEvent);
            setKeepMouseGrab(false);
            setKeepTouchGrab(false);
            m_touchPoints.clear();
        }
        return;
    }

    Qt::TouchPointStates touchStates = 0;
    QList<int> removedTouches;

    const QList<QTouchEvent::TouchPoint> &touchPoints = event->touchPoints();
    foreach (QTouchEvent::TouchPoint touchPoint, touchPoints) {

        int touchIdx = findTouch(touchPoint.id());
        if (touchIdx >= 0)
            m_touchPoints[touchIdx] = touchPoint;

        switch (touchPoint.state()) {
        case Qt::TouchPointPressed:
            if (touchIdx >= 0)
                continue;
            touchStates |= Qt::TouchPointPressed;
            m_touchPoints.append(touchPoint);
            if (m_touchPoints.count() > 1) {
                setKeepMouseGrab(true);
                setKeepTouchGrab(true);
                grabMouse();
                grabTouchPoints(QVector<int>() << touchPoint.id());
            } else {
                m_startPos = touchPoint.scenePos();
                qDebug() << "TOUCH BEGIN" << m_startPos;
                setKeepMouseGrab(false);
                setKeepTouchGrab(false);
            }
            break;
        // fall through
        case Qt::TouchPointMoved: {
            if (touchIdx < 0)
                continue;
            if (QQuickWindowPrivate::get(win)->touchMouseId == touchPoint.id()) {
                const int dragThreshold = QGuiApplication::styleHints()->startDragDistance();
                QPointF delta = touchPoint.scenePos() - m_startPos;
                if (!keepMouseGrab()) {
                    qDebug() << "PAST THRESH" << delta.y() << atYBeginning() << atYEnd();
                    if ((delta.y() >= dragThreshold && !atYBeginning()) || (delta.y() <= -dragThreshold && !atYEnd())
                            || (delta.x() >= dragThreshold && !atXBeginning()) || (delta.x() <= -dragThreshold && !atXEnd())) {
                        setKeepMouseGrab(true);
                        setKeepTouchGrab(true);
                        grabMouse();
                        grabTouchPoints(QVector<int>() << touchPoint.id());
                        qDebug() << "I WANT THE GRAB";
                    }
                    // Do not pass this event through
                    bool keeping = grabber ? grabber->keepMouseGrab() : false;
                    qDebug() << "***********************************************************" << grabber << keeping;
                    return;
                } else if (grabber && grabber != this && grabber->keepMouseGrab()) {
                    qDebug() << "Don't have grab - ignore";
                    m_touchPoints.removeAt(touchIdx);
                    event->ignore();
                    return;
                }
            }
            touchStates |= Qt::TouchPointMoved;
            break;
        }
        case Qt::TouchPointReleased: {
            qDebug() << "TOUCH: END";
            touchStates |= Qt::TouchPointReleased;
            removedTouches << touchPoint.id();
            break;
        }
        default:
            break;
        }
    }


    QTouchEvent localEvent(*event);
    localEvent.setTouchPoints(m_touchPoints);
    localEvent.setTouchPointStates(touchStates);
    QuickMozView::touchEvent(&localEvent);
    event->setAccepted(localEvent.isAccepted());

    foreach (int id, removedTouches) {
        int touchIdx = findTouch(id);
        if (touchIdx >= 0) {
            qDebug() << "REMOVED TOUCH" << touchIdx << id;
            m_touchPoints.removeAt(touchIdx);
        }
    }

    qDebug() << "<<<<<<<<<<<<<< TOUCH COUNT" << m_touchPoints.count() << touchStates;

    if (m_touchPoints.isEmpty()) {
        qDebug() << "----------------------- ALL CLEAR ----------------------";
        setKeepMouseGrab(false);
        setKeepTouchGrab(false);
    }
}

bool RawWebView::childMouseEventFilter(QQuickItem *i, QEvent *e)
{
    if (!isVisible())
        return QQuickItem::childMouseEventFilter(i, e);
    switch (e->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
        qDebug() << "FILTERING" << e;
        handleTouchEvent(static_cast<QTouchEvent*>(e));
        e->setAccepted(keepMouseGrab());
        return keepMouseGrab();
    case QEvent::TouchEnd:
        qDebug() << "FILTERING" << e;
        handleTouchEvent(static_cast<QTouchEvent*>(e));
        break;
    default:
        break;
    }

    return QQuickItem::childMouseEventFilter(i, e);
}

} // namespace WebView

} // namespace SailfishOS

