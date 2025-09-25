// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "protocols/imagecopycapture.h"

#include <QQuickItem>

class ImageCopyCaptureItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(State state READ state WRITE setState NOTIFY stateChanged FINAL)
    QML_ELEMENT
public:
    enum State {
        Paused,
        Streaming,
    };
    Q_ENUM(State);

    ImageCopyCaptureItem();

    State state() const { return m_state; }
    void setState(State state);

    ImageCopyCaptureSession *session() const { return m_session; }
    void setSession(ImageCopyCaptureSession *session);

Q_SIGNALS:
    void stateChanged();
    void sessionChanged();

private:
    void updateTextureDmaBuf();
    void updateTextureImage();

private:
    State m_state = Streaming;
    ImageCopyCaptureSession *m_session = nullptr;
};
