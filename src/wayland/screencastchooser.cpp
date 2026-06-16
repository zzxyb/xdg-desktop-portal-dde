// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencastchooser.h"
#include "screenlistmodel.h"
#include "toplevelmodel.h"
#include "loggings.h"
#include "amhelper.h"

#include <QSettings>
#include <QStandardPaths>
#include <QQuickGraphicsConfiguration>
#include <vulkan/vulkan.h>

constexpr auto URI = "screencast";

ScreenCastChooser::ScreenCastChooser( const QString &appID, PortalCommon::SourceTypes types, QObject *parent)
    : QObject(parent)
    , m_engine(new QQmlApplicationEngine(this))
    , m_window(nullptr)
{
    QObject::connect(m_engine, &QQmlApplicationEngine::objectCreationFailed, this, [this](){
        qCCritical(SCREENCAST, "qml create failed!");
    });

    m_engine->load(QUrl("qrc:/screencast/ScreencastChooserWindow.qml"));
    QObject *rootObject =  m_engine->rootObjects().first();
    QQuickWindow* win = qobject_cast<QQuickWindow *>(rootObject);

    if (win) {
        QQuickGraphicsConfiguration graphicsConfiguration = win->graphicsConfiguration();
        graphicsConfiguration.setDeviceExtensions({
            QByteArrayLiteral(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME),
            QByteArrayLiteral(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME),
            QByteArrayLiteral(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME),
            QByteArrayLiteral(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME),
            QByteArrayLiteral(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME),
            QByteArrayLiteral(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME),
            QByteArrayLiteral(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME),
            QByteArrayLiteral(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME)
        });
        win->setGraphicsConfiguration(graphicsConfiguration);
        connect(win, &QQuickWindow::closing, this, &ScreenCastChooser::handleWindowClosed);
        connect(win, SIGNAL(accept()), this, SLOT(accept()));
        connect(win, SIGNAL(reject()), this, SLOT(reject()));
        m_window = win;
        m_window->setProperty("clientAppName", QVariant::fromValue(AMHelpers::nameFromAM(appID)));
    }
}

ScreenCastChooser::~ScreenCastChooser()
{
    closeWindow();
}

void ScreenCastChooser::showWindow()
{
    if (m_window) {
        m_window->show();
        m_window->raise();
        m_window->requestActivate();
    }
}

void ScreenCastChooser::closeWindow()
{
    if (m_window) {
        m_window->close();
    }
}

void ScreenCastChooser::handleWindowClosed()
{
    QQuickWindow* win = qobject_cast<QQuickWindow *>(sender());
    if (win) {
        win->deleteLater();
    }
}

QRect ScreenCastChooser::selectedRegion() const
{
    // TODO
    return QRect();
}

QList<QPointer<QScreen>> ScreenCastChooser::selectedOutputs() const
{
    if (m_window->property("viewLayoutIndex").toInt() != 0) {
        return {};
    }

    ScreenListModel *model = dynamic_cast<ScreenListModel *>
            (m_window->property("outputsModel").value<QObject *>());
    if (!model) {
        return {};
    }
    return model->selectedOutputs(m_window->property("outputIndex").toInt());
}

QList<ToplevelInfo *> ScreenCastChooser::selectedToplevels() const
{
    if (m_window->property("viewLayoutIndex").toInt() != 1) {
        return {};
    }

    ToplevelListModel *model = dynamic_cast<ToplevelListModel *>
            (m_window->property("toplevelsModel").value<QObject *>());
    if (!model) {
        return {};
    }
    return model->selectedToplevels(m_window->property("toplevelIndex").toInt());
}

bool ScreenCastChooser::allowRestore() const
{
    return m_window->property("allowRestore").toBool();
}

QWindow *ScreenCastChooser::windowHandle() const
{
    if (m_window) {
        return m_window;
    }

    return nullptr;
}

void ScreenCastChooser::reject()
{
    Q_EMIT rejected();
    Q_EMIT finished(ScreenCastChooser::Rejected);
    deleteLater();
}

void ScreenCastChooser::accept()
{
    Q_EMIT accepted();
    Q_EMIT finished(ScreenCastChooser::Accepted);
    deleteLater();
}
