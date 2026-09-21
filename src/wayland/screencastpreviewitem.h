// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "screencastpreview.h"

#include <QQuickRhiItem>
#include <QtQmlIntegration>

class ScreenCastPreviewItem : public QQuickRhiItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int sourceType READ sourceType WRITE setSourceType NOTIFY sourceTypeChanged FINAL)
    Q_PROPERTY(QObject *outputsModel READ outputsModel WRITE setOutputsModel NOTIFY outputsModelChanged FINAL)
    Q_PROPERTY(int outputIndex READ outputIndex WRITE setOutputIndex NOTIFY outputIndexChanged FINAL)
    Q_PROPERTY(QObject *toplevelsModel READ toplevelsModel WRITE setToplevelsModel NOTIFY toplevelsModelChanged FINAL)
    Q_PROPERTY(int toplevelIndex READ toplevelIndex WRITE setToplevelIndex NOTIFY toplevelIndexChanged FINAL)
    Q_PROPERTY(bool showCursor READ showCursor WRITE setShowCursor NOTIFY showCursorChanged FINAL)

public:
    explicit ScreenCastPreviewItem(QQuickItem *parent = nullptr);
    ~ScreenCastPreviewItem() override;

    int sourceType() const;
    void setSourceType(int sourceType);

    QObject *outputsModel() const;
    void setOutputsModel(QObject *model);

    int outputIndex() const;
    void setOutputIndex(int index);

    QObject *toplevelsModel() const;
    void setToplevelsModel(QObject *model);

    int toplevelIndex() const;
    void setToplevelIndex(int index);

    bool showCursor() const;
    void setShowCursor(bool show);

    ScreenCastPreview *preview() const;

Q_SIGNALS:
    void sourceTypeChanged();
    void outputsModelChanged();
    void outputIndexChanged();
    void toplevelsModelChanged();
    void toplevelIndexChanged();
    void showCursorChanged();

protected:
    QQuickRhiItemRenderer *createRenderer() override;

private:
    ScreenCastPreview *m_preview;
};
