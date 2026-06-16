// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencastpreviewpainteditem.h"

#include <QPainter>

ScreenCastPreviewPaintedItem::ScreenCastPreviewPaintedItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_preview(new ScreenCastPreview(this))
{
    setRenderTarget(QQuickPaintedItem::Image);
    setAntialiasing(true);
    connect(m_preview, &ScreenCastPreview::frameChanged, this, [this] {
        update();
    });
}

ScreenCastPreviewPaintedItem::~ScreenCastPreviewPaintedItem() = default;

int ScreenCastPreviewPaintedItem::sourceType() const
{
    return m_preview->sourceType();
}

void ScreenCastPreviewPaintedItem::setSourceType(int sourceType)
{
    if (m_preview->sourceType() == sourceType) {
        return;
    }

    m_preview->setSourceType(sourceType);
    Q_EMIT sourceTypeChanged();
}

QObject *ScreenCastPreviewPaintedItem::outputsModel() const
{
    return m_preview->outputsModel();
}

void ScreenCastPreviewPaintedItem::setOutputsModel(QObject *model)
{
    if (m_preview->outputsModel() == model) {
        return;
    }

    m_preview->setOutputsModel(model);
    Q_EMIT outputsModelChanged();
}

int ScreenCastPreviewPaintedItem::outputIndex() const
{
    return m_preview->outputIndex();
}

void ScreenCastPreviewPaintedItem::setOutputIndex(int index)
{
    if (m_preview->outputIndex() == index) {
        return;
    }

    m_preview->setOutputIndex(index);
    Q_EMIT outputIndexChanged();
}

QObject *ScreenCastPreviewPaintedItem::toplevelsModel() const
{
    return m_preview->toplevelsModel();
}

void ScreenCastPreviewPaintedItem::setToplevelsModel(QObject *model)
{
    if (m_preview->toplevelsModel() == model) {
        return;
    }

    m_preview->setToplevelsModel(model);
    Q_EMIT toplevelsModelChanged();
}

int ScreenCastPreviewPaintedItem::toplevelIndex() const
{
    return m_preview->toplevelIndex();
}

void ScreenCastPreviewPaintedItem::setToplevelIndex(int index)
{
    if (m_preview->toplevelIndex() == index) {
        return;
    }

    m_preview->setToplevelIndex(index);
    Q_EMIT toplevelIndexChanged();
}

bool ScreenCastPreviewPaintedItem::showCursor() const
{
    return m_preview->showCursor();
}

void ScreenCastPreviewPaintedItem::setShowCursor(bool show)
{
    if (m_preview->showCursor() == show) {
        return;
    }

    m_preview->setShowCursor(show);
    Q_EMIT showCursorChanged();
}

ScreenCastPreview *ScreenCastPreviewPaintedItem::preview() const
{
    return m_preview;
}

void ScreenCastPreviewPaintedItem::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->fillRect(boundingRect(), Qt::transparent);

    const QImage frame = m_preview->frame();
    if (frame.isNull()) {
        return;
    }

    painter->drawImage(targetRect(frame.size()), frame);
}

QRectF ScreenCastPreviewPaintedItem::targetRect(const QSize &imageSize) const
{
    if (!imageSize.isValid() || width() <= 0 || height() <= 0) {
        return {};
    }

    const qreal imageAspect = qreal(imageSize.width()) / qreal(imageSize.height());
    const qreal itemAspect = width() / height();

    qreal targetWidth = width();
    qreal targetHeight = height();
    if (imageAspect > itemAspect) {
        targetHeight = width() / imageAspect;
    } else {
        targetWidth = height() * imageAspect;
    }

    return QRectF((width() - targetWidth) / 2.0,
                  (height() - targetHeight) / 2.0,
                  targetWidth,
                  targetHeight);
}
