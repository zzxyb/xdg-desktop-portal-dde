// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencastsession.h"

ScreenCastSession::ScreenCastSession(const QString &appId,
                                     const QString &path,
                                     const QString &iconName,
                                     QObject *parent)
    : Session(appId, path, parent)
{
}

ScreenCastSession::~ScreenCastSession()
{
}

bool ScreenCastSession::multipleSources() const
{
    return m_multipleSources;
}

PortalCommon::SourceTypes ScreenCastSession::types() const
{
    return m_types;
}

void ScreenCastSession::setPersistMode(PortalCommon::PersistMode persistMode)
{
    m_persistMode = persistMode;
}

void ScreenCastSession::setStreams(const Streams &streams)
{
    Q_ASSERT(!streams.isEmpty());

    m_streams = streams;
}

PortalCommon::CursorModes ScreenCastSession::cursorMode() const
{
    return m_cursorMode;
}

bool ScreenCastSession::setOptions(const QVariantMap &options,
                                   PortalCommon::SourceTypes availableSourceTypes,
                                   uint availableCursorModes)
{
    m_multipleSources = options.value(QStringLiteral("multiple")).toBool();
    const uint cursorMode = options.value(QStringLiteral("cursor_mode"),
                                          uint(PortalCommon::Hidden)).toUInt();
    const uint sourceTypes = options.value(QStringLiteral("types"),
                                           uint(PortalCommon::Monitor)).toUInt();

    if (cursorMode == 0 || (cursorMode & (cursorMode - 1)) != 0 ||
        (cursorMode & availableCursorModes) == 0) {
        return false;
    }

    const uint availableTypes = uint(availableSourceTypes);
    if (sourceTypes == 0 || (sourceTypes & ~availableTypes) != 0) {
        return false;
    }

    m_cursorMode = PortalCommon::CursorModes(cursorMode);
    m_types = PortalCommon::SourceTypes(PortalCommon::SourceType(sourceTypes));
    return true;
}
