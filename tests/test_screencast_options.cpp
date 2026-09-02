// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencastsession.h"

#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ScreenCastSession session{QString(), QString(), QString()};
    const auto availableTypes = PortalCommon::SourceTypes(PortalCommon::Monitor |
                                                           PortalCommon::Window);
    const uint availableCursorModes = PortalCommon::Hidden | PortalCommon::Embedded;

    if (!session.setOptions({}, availableTypes, availableCursorModes) ||
        session.types() != PortalCommon::Monitor ||
        session.cursorMode() != PortalCommon::Hidden) {
        return 1;
    }

    if (session.setOptions({{QStringLiteral("cursor_mode"), 0u}},
                           availableTypes, availableCursorModes)) {
        return 2;
    }

    if (session.setOptions({{QStringLiteral("cursor_mode"), uint(PortalCommon::Metadata)}},
                           availableTypes, availableCursorModes)) {
        return 3;
    }

    if (session.setOptions({{QStringLiteral("types"), 4u}},
                           availableTypes, availableCursorModes)) {
        return 4;
    }

    const QVariantMap combinedTypes{
        {QStringLiteral("types"), uint(PortalCommon::Monitor | PortalCommon::Window)},
        {QStringLiteral("cursor_mode"), uint(PortalCommon::Embedded)},
        {QStringLiteral("multiple"), true},
    };
    if (!session.setOptions(combinedTypes, availableTypes, availableCursorModes) ||
        session.types() != availableTypes ||
        session.cursorMode() != PortalCommon::Embedded ||
        !session.multipleSources()) {
        return 5;
    }

    return 0;
}
