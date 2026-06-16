// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "portalcommon.h"
#include "protocols/imagecopycapture.h"
#include "screenlistmodel.h"
#include "screencastcontext.h"
#include "toplevelmodel.h"

#include <QObject>
#include <QImage>
#include <QPointer>
#include <QtQmlIntegration>
#include <memory>

struct wl_buffer;

class PreviewShmBuffer;
class PreviewDmaBufBuffer;

struct PreviewDmaBufFrame {
    int fd = -1;
    QSize size;
    uint32_t format = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;
    uint64_t modifier = 0;
    uint32_t transform = 0;
    quintptr token = 0;
    quint64 serial = 0;

    bool isValid() const { return fd >= 0 && size.isValid() && token != 0; }
};

class ScreenCastPreview : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int sourceType READ sourceType WRITE setSourceType NOTIFY sourceTypeChanged FINAL)
    Q_PROPERTY(QObject *outputsModel READ outputsModel WRITE setOutputsModel NOTIFY outputsModelChanged FINAL)
    Q_PROPERTY(int outputIndex READ outputIndex WRITE setOutputIndex NOTIFY outputIndexChanged FINAL)
    Q_PROPERTY(QObject *toplevelsModel READ toplevelsModel WRITE setToplevelsModel NOTIFY toplevelsModelChanged FINAL)
    Q_PROPERTY(int toplevelIndex READ toplevelIndex WRITE setToplevelIndex NOTIFY toplevelIndexChanged FINAL)
    Q_PROPERTY(bool showCursor READ showCursor WRITE setShowCursor NOTIFY showCursorChanged FINAL)
    Q_PROPERTY(QImage frame READ frame NOTIFY frameChanged FINAL)
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameChanged FINAL)

public:
    explicit ScreenCastPreview(QObject *parent = nullptr);
    ~ScreenCastPreview() override;

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

    QImage frame() const;
    QSize frameSize() const;
    quint64 frameSerial() const { return m_frameSerial; }
    PreviewDmaBufFrame dmaBufFrame() const { return m_dmaBufFrame; }
    void setPreferDmaBuf(bool prefer) { m_preferDmaBuf = prefer; }
    Q_INVOKABLE void releaseDmaBufFrame(quintptr token);
    Q_INVOKABLE void fallbackToShm();
    static QImage::Format toImageFormat(wl_shm_format format);

Q_SIGNALS:
    void sourceTypeChanged();
    void outputsModelChanged();
    void outputIndexChanged();
    void toplevelsModelChanged();
    void toplevelIndexChanged();
    void showCursorChanged();
    void frameChanged();

private Q_SLOTS:
    void handleSessionBufferSizeChanged(uint32_t width, uint32_t height);
    void handleSessionShmFormatChanged(uint32_t format);
    void handleSessionDmaBufDeviceChanged(wl_array *device);
    void handleSessionDmaBufFormatChanged(uint32_t format, wl_array *modifiers);
    void handleSessionDone();
    void handleSessionStopped();
    void handleFrameReady();
    void handleFrameFailed(uint32_t reason);
    void handleCursorEnter();
    void handleCursorLeave();
    void handleCursorPositionChanged(int32_t x, int32_t y);
    void handleCursorHotspotChanged(int32_t x, int32_t y);

private:
    enum class CaptureKind {
        Source,
        Cursor,
    };

    struct CaptureState {
        CaptureKind kind = CaptureKind::Source;
        ImageCopyCaptureSession *session = nullptr;
        ImageCopyCaptureFrame *frame = nullptr;
        QList<uint32_t> shmFormats;
        QList<QPair<uint32_t, uint64_t>> dmaBufFormats;
        gbm_device *gbm = nullptr;
        QSize size;
        uint32_t transform = 0;
        wl_shm_format shmFormat = WL_SHM_FORMAT_XRGB8888;
        bool stopped = false;
    };

    void scheduleRestart();
    void restart();
    void clearState();
    void initializeSourceSession();
    void initializeCursorSession();
    void destroySession(CaptureState &state);
    void captureNext(CaptureState &state);
    void ensureBuffers(CaptureState &state);
    PreviewDmaBufBuffer *createDmaBufBuffer(CaptureState &state, uint32_t format, uint64_t modifier);
    PreviewShmBuffer *acquireBuffer(CaptureState &state) const;
    PreviewDmaBufBuffer *acquireDmaBufBuffer(CaptureState &state) const;
    void releaseBuffers(QList<PreviewShmBuffer *> &buffers);
    void processFrame(CaptureState &state);
    void updateCompositedFrame();
    QImage transformImage(const QImage &image, uint32_t transform) const;
    QScreen *selectedOutput() const;
    ToplevelInfo *selectedToplevel() const;
    static bool isSupportedShmFormat(uint32_t format);

    QPointer<ScreenCastContext> m_context;
    QPointer<ScreenListModel> m_outputsModel;
    QPointer<ToplevelListModel> m_toplevelsModel;

    CaptureState m_sourceState;
    CaptureState m_cursorState;

    QList<PreviewShmBuffer *> m_sourceBuffers;
    QList<PreviewShmBuffer *> m_cursorBuffers;
    QList<PreviewDmaBufBuffer *> m_dmaBufBuffers;
    struct ext_image_capture_source_v1 *m_source = nullptr;
    ImageCopyCaptureCursorSession *m_cursorSession = nullptr;

    int m_sourceType = PortalCommon::Monitor;
    int m_outputIndex = -1;
    int m_toplevelIndex = -1;
    bool m_showCursor = true;
    bool m_cursorVisible = false;
    QPoint m_cursorPosition;
    QPoint m_cursorHotspot;

    QImage m_sourceImage;
    QImage m_cursorImage;
    QImage m_frame;
    quint64 m_frameSerial = 0;
    quint64 m_dmaBufSerial = 0;
    PreviewDmaBufFrame m_dmaBufFrame;
    bool m_preferDmaBuf = false;
    bool m_usingDmaBuf = false;
    bool m_restartPending = false;
};
