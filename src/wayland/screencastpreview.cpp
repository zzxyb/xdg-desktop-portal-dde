// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencastpreview.h"

#include "loggings.h"
#include "protocols/common.h"

#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformnativeinterface.h>

#include <QGuiApplication>
#include <QPainter>

#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <drm_fourcc.h>

class PreviewShmBuffer
{
public:
    ~PreviewShmBuffer()
    {
        if (buffer) {
            wl_buffer_destroy(buffer);
        }
        if (data && data != MAP_FAILED) {
            munmap(data, sizeInBytes);
        }
        if (fd >= 0) {
            close(fd);
        }
    }

    int fd = -1;
    void *data = nullptr;
    wl_buffer *buffer = nullptr;
    qsizetype sizeInBytes = 0;
    QSize size;
    int stride = 0;
    wl_shm_format format = WL_SHM_FORMAT_XRGB8888;
    bool busy = false;
};

class PreviewDmaBufBuffer
{
public:
    ~PreviewDmaBufBuffer()
    {
        if (buffer)
            wl_buffer_destroy(buffer);
        if (fd >= 0)
            close(fd);
        if (bo)
            gbm_bo_destroy(bo);
    }

    gbm_bo *bo = nullptr;
    wl_buffer *buffer = nullptr;
    int fd = -1;
    QSize size;
    uint32_t format = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;
    uint64_t modifier = DRM_FORMAT_MOD_INVALID;
    bool busy = false;
    bool retired = false;
};

namespace {

static void randname(char *buf)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

static int anonymousShmOpen()
{
    char name[] = "/xdp-preview-XXXXXX";
    int retries = 100;

    do {
        randname(name + strlen(name) - 6);
        --retries;
        const int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);

    return -1;
}

static PreviewShmBuffer *createPreviewBuffer(ScreenCastContext *context, const QSize &size, wl_shm_format format)
{
    if (!context || !size.isValid()) {
        return nullptr;
    }

    const QImage::Format imageFormat = ScreenCastPreview::toImageFormat(format);
    if (imageFormat == QImage::Format_Invalid) {
        return nullptr;
    }

    auto *previewBuffer = new PreviewShmBuffer;
    previewBuffer->size = size;
    previewBuffer->format = format;
    previewBuffer->stride = size.width() * 4;
    previewBuffer->sizeInBytes = qsizetype(previewBuffer->stride) * size.height();
    previewBuffer->fd = anonymousShmOpen();
    if (previewBuffer->fd < 0) {
        delete previewBuffer;
        return nullptr;
    }

    if (ftruncate(previewBuffer->fd, previewBuffer->sizeInBytes) < 0) {
        delete previewBuffer;
        return nullptr;
    }

    previewBuffer->data = mmap(nullptr,
                               previewBuffer->sizeInBytes,
                               PROT_READ | PROT_WRITE,
                               MAP_SHARED,
                               previewBuffer->fd,
                               0);
    if (previewBuffer->data == MAP_FAILED) {
        previewBuffer->data = nullptr;
        delete previewBuffer;
        return nullptr;
    }

    previewBuffer->buffer = context->createWLSHMBuffer(previewBuffer->fd,
                                                       format,
                                                       size.width(),
                                                       size.height(),
                                                       previewBuffer->stride);
    if (!previewBuffer->buffer) {
        delete previewBuffer;
        return nullptr;
    }

    return previewBuffer;
}

static int imageCopyCaptureOptions(bool paintCursor)
{
    return paintCursor ? EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS : 0;
}

} // namespace

PreviewDmaBufBuffer *ScreenCastPreview::createDmaBufBuffer(CaptureState &state,
                                                            uint32_t format,
                                                            uint64_t modifier)
{
    if (!m_context || !state.gbm || !state.size.isValid() || !m_context->linuxDmaBufInterfaceActive())
        return nullptr;

    auto *result = new PreviewDmaBufBuffer;
    result->size = state.size;
    result->format = format;
    result->modifier = modifier;
    const uint32_t flags = GBM_BO_USE_RENDERING;
    if (modifier != DRM_FORMAT_MOD_INVALID) {
        result->bo = gbm_bo_create_with_modifiers2(state.gbm, state.size.width(), state.size.height(),
                                                    format, &modifier, 1, flags);
    } else {
        result->bo = gbm_bo_create(state.gbm, state.size.width(), state.size.height(), format, flags);
    }
    if (!result->bo || gbm_bo_get_plane_count(result->bo) != 1) {
        delete result;
        return nullptr;
    }

    result->modifier = gbm_bo_get_modifier(result->bo);
    result->stride = gbm_bo_get_stride_for_plane(result->bo, 0);
    result->offset = gbm_bo_get_offset(result->bo, 0);
    result->fd = gbm_bo_get_fd_for_plane(result->bo, 0);
    if (result->fd < 0) {
        delete result;
        return nullptr;
    }

    auto *params = m_context->m_linuxDmaBuf->create_params();
    if (!params) {
        delete result;
        return nullptr;
    }
    zwp_linux_buffer_params_v1_add(params, result->fd, 0, result->offset, result->stride,
                                   result->modifier >> 32, result->modifier & 0xffffffff);
    result->buffer = zwp_linux_buffer_params_v1_create_immed(params, state.size.width(), state.size.height(),
                                                             format, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    if (!result->buffer) {
        delete result;
        return nullptr;
    }
    return result;
}

ScreenCastPreview::ScreenCastPreview(QObject *parent)
    : QObject(parent)
    , m_context(new ScreenCastContext(this))
{
    m_sourceState.kind = CaptureKind::Source;
    m_cursorState.kind = CaptureKind::Cursor;
}

ScreenCastPreview::~ScreenCastPreview()
{
    clearState();
}

int ScreenCastPreview::sourceType() const
{
    return m_sourceType;
}

void ScreenCastPreview::setSourceType(int sourceType)
{
    if (m_sourceType == sourceType) {
        return;
    }

    m_sourceType = sourceType;
    Q_EMIT sourceTypeChanged();
    scheduleRestart();
}

QObject *ScreenCastPreview::outputsModel() const
{
    return m_outputsModel;
}

void ScreenCastPreview::setOutputsModel(QObject *model)
{
    auto *outputsModel = qobject_cast<ScreenListModel *>(model);
    if (m_outputsModel == outputsModel) {
        return;
    }

    m_outputsModel = outputsModel;
    Q_EMIT outputsModelChanged();
    if (m_sourceType == PortalCommon::Monitor) {
        scheduleRestart();
    }
}

int ScreenCastPreview::outputIndex() const
{
    return m_outputIndex;
}

void ScreenCastPreview::setOutputIndex(int index)
{
    if (m_outputIndex == index) {
        return;
    }

    m_outputIndex = index;
    Q_EMIT outputIndexChanged();
    if (m_sourceType == PortalCommon::Monitor) {
        scheduleRestart();
    }
}

QObject *ScreenCastPreview::toplevelsModel() const
{
    return m_toplevelsModel;
}

void ScreenCastPreview::setToplevelsModel(QObject *model)
{
    auto *toplevelsModel = qobject_cast<ToplevelListModel *>(model);
    if (m_toplevelsModel == toplevelsModel) {
        return;
    }

    m_toplevelsModel = toplevelsModel;
    Q_EMIT toplevelsModelChanged();
    if (m_sourceType == PortalCommon::Window) {
        scheduleRestart();
    }
}

int ScreenCastPreview::toplevelIndex() const
{
    return m_toplevelIndex;
}

void ScreenCastPreview::setToplevelIndex(int index)
{
    if (m_toplevelIndex == index) {
        return;
    }

    m_toplevelIndex = index;
    Q_EMIT toplevelIndexChanged();
    if (m_sourceType == PortalCommon::Window) {
        scheduleRestart();
    }
}

bool ScreenCastPreview::showCursor() const
{
    return m_showCursor;
}

void ScreenCastPreview::setShowCursor(bool show)
{
    if (m_showCursor == show) {
        return;
    }

    m_showCursor = show;
    Q_EMIT showCursorChanged();
    scheduleRestart();
}

QImage ScreenCastPreview::frame() const
{
    return m_frame;
}

QSize ScreenCastPreview::frameSize() const
{
    return m_frame.size();
}

void ScreenCastPreview::handleSessionBufferSizeChanged(uint32_t width, uint32_t height)
{
    auto *session = qobject_cast<ImageCopyCaptureSession *>(sender());
    CaptureState *state = session == m_cursorState.session ? &m_cursorState : &m_sourceState;
    state->size = QSize(int(width), int(height));
}

void ScreenCastPreview::handleSessionShmFormatChanged(uint32_t format)
{
    auto *session = qobject_cast<ImageCopyCaptureSession *>(sender());
    CaptureState *state = session == m_cursorState.session ? &m_cursorState : &m_sourceState;
    if (isSupportedShmFormat(format) && !state->shmFormats.contains(format)) {
        state->shmFormats.append(format);
    }
}

void ScreenCastPreview::handleSessionDmaBufDeviceChanged(wl_array *deviceArray)
{
    auto *session = qobject_cast<ImageCopyCaptureSession *>(sender());
    CaptureState *state = session == m_cursorState.session ? &m_cursorState : &m_sourceState;
    if (deviceArray->size != sizeof(dev_t))
        return;
    dev_t deviceId;
    memcpy(&deviceId, deviceArray->data, sizeof(deviceId));
    drmDevice *device = nullptr;
    if (drmGetDeviceFromDevId(deviceId, 0, &device) != 0)
        return;
    if (state->gbm) {
        const int fd = gbm_device_get_fd(state->gbm);
        gbm_device_destroy(state->gbm);
        close(fd);
    }
    state->gbm = ScreenCastContext::createGBMDeviceFromDRMDevice(device);
    drmFreeDevice(&device);
}

void ScreenCastPreview::handleSessionDmaBufFormatChanged(uint32_t format, wl_array *modifiers)
{
    auto *session = qobject_cast<ImageCopyCaptureSession *>(sender());
    CaptureState *state = session == m_cursorState.session ? &m_cursorState : &m_sourceState;
    const char *end = static_cast<const char *>(modifiers->data) + modifiers->size;
    for (auto *modifier = static_cast<uint64_t *>(modifiers->data);
         reinterpret_cast<const char *>(modifier) < end; ++modifier) {
        const auto pair = qMakePair(format, *modifier);
        if (!state->dmaBufFormats.contains(pair))
            state->dmaBufFormats.append(pair);
    }
}

void ScreenCastPreview::handleSessionDone()
{
    auto *session = qobject_cast<ImageCopyCaptureSession *>(sender());
    CaptureState *state = session == m_cursorState.session ? &m_cursorState : &m_sourceState;
    state->stopped = false;
    if (!state->size.isValid()) {
        qCWarning(SCREENCAST) << "preview session missing buffer size";
        return;
    }

    if (m_preferDmaBuf && state->kind == CaptureKind::Source && state->gbm
        && !state->dmaBufFormats.isEmpty()) {
        for (const auto &[format, modifier] : std::as_const(state->dmaBufFormats)) {
            if (format != DRM_FORMAT_XRGB8888 && format != DRM_FORMAT_ARGB8888)
                continue;
            for (int i = 0; i < 3; ++i) {
                if (auto *buffer = createDmaBufBuffer(*state, format, modifier))
                    m_dmaBufBuffers.append(buffer);
            }
            if (!m_dmaBufBuffers.isEmpty()) {
                m_usingDmaBuf = true;
                captureNext(*state);
                return;
            }
        }
        qCWarning(SCREENCAST) << "failed to allocate preview dmabufs, falling back to shm";
    }
    if (state->shmFormats.isEmpty()) {
        qCWarning(SCREENCAST) << "preview session has no usable shm fallback";
        return;
    }
    state->shmFormat = wl_shm_format(state->shmFormats.constFirst());
    ensureBuffers(*state);
    captureNext(*state);
}

void ScreenCastPreview::handleSessionStopped()
{
    auto *session = qobject_cast<ImageCopyCaptureSession *>(sender());
    CaptureState *state = session == m_cursorState.session ? &m_cursorState : &m_sourceState;
    state->stopped = true;
}

void ScreenCastPreview::handleFrameReady()
{
    auto *frame = qobject_cast<ImageCopyCaptureFrame *>(sender());
    CaptureState *state = frame == m_cursorState.frame ? &m_cursorState : &m_sourceState;
    processFrame(*state);
    captureNext(*state);
}

void ScreenCastPreview::handleFrameFailed(uint32_t reason)
{
    auto *frame = qobject_cast<ImageCopyCaptureFrame *>(sender());
    CaptureState *state = frame == m_cursorState.frame ? &m_cursorState : &m_sourceState;
    auto *buffer = reinterpret_cast<PreviewShmBuffer *>(
            frame->property("previewBuffer").value<quintptr>());
    if (buffer) {
        buffer->busy = false;
    }
    auto *dmaBuf = reinterpret_cast<PreviewDmaBufBuffer *>(
            frame->property("previewDmaBuf").value<quintptr>());
    if (dmaBuf)
        dmaBuf->busy = false;
    if (state->frame) {
        state->frame->destroy();
        state->frame->deleteLater();
        state->frame = nullptr;
    }
    if (reason != EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED) {
        captureNext(*state);
    }
}

void ScreenCastPreview::handleCursorEnter()
{
    m_cursorVisible = true;
}

void ScreenCastPreview::handleCursorLeave()
{
    m_cursorVisible = false;
    updateCompositedFrame();
}

void ScreenCastPreview::handleCursorPositionChanged(int32_t x, int32_t y)
{
    m_cursorPosition = QPoint(x, y);
    updateCompositedFrame();
}

void ScreenCastPreview::handleCursorHotspotChanged(int32_t x, int32_t y)
{
    m_cursorHotspot = QPoint(x, y);
    updateCompositedFrame();
}

void ScreenCastPreview::scheduleRestart()
{
    if (m_restartPending) {
        return;
    }

    m_restartPending = true;
    QMetaObject::invokeMethod(this, [this] {
        m_restartPending = false;
        restart();
    }, Qt::QueuedConnection);
}

void ScreenCastPreview::restart()
{
    clearState();
    initializeSourceSession();
    initializeCursorSession();
}

void ScreenCastPreview::clearState()
{
    destroySession(m_sourceState);
    destroySession(m_cursorState);

    if (m_cursorSession) {
        m_cursorSession->destroySession();
        delete m_cursorSession;
        m_cursorSession = nullptr;
    }
    if (m_source) {
        ext_image_capture_source_v1_destroy(m_source);
        m_source = nullptr;
    }

    releaseBuffers(m_sourceBuffers);
    releaseBuffers(m_cursorBuffers);
    qDeleteAll(m_dmaBufBuffers);
    m_dmaBufBuffers.clear();
    if (m_sourceState.gbm) {
        const int fd = gbm_device_get_fd(m_sourceState.gbm);
        gbm_device_destroy(m_sourceState.gbm);
        close(fd);
        m_sourceState.gbm = nullptr;
    }
    if (m_cursorState.gbm) {
        const int fd = gbm_device_get_fd(m_cursorState.gbm);
        gbm_device_destroy(m_cursorState.gbm);
        close(fd);
        m_cursorState.gbm = nullptr;
    }

    m_cursorVisible = false;
    m_cursorPosition = {};
    m_cursorHotspot = {};
    m_sourceImage = QImage();
    m_cursorImage = QImage();
    m_frame = QImage();
    m_dmaBufFrame = {};
    m_usingDmaBuf = false;
    ++m_frameSerial;
    Q_EMIT frameChanged();
}

void ScreenCastPreview::initializeSourceSession()
{
    if (!m_context || !m_context->imageCopyCaptureManagerActive()) {
        return;
    }

    if (m_sourceType == PortalCommon::Monitor) {
        QScreen *output = selectedOutput();
        if (!output || !m_context->outputImageCaptureSourceManagerActive()) {
            return;
        }

        auto nativeInterface = qGuiApp->platformNativeInterface();
        auto *wlOutput = reinterpret_cast<wl_output *>(
                nativeInterface->nativeResourceForScreen(QByteArrayLiteral("output"), output));
        if (!wlOutput) {
            return;
        }

        m_source = m_context->m_outputImageCaptureSourceManager->create_source(wlOutput);
    } else if (m_sourceType == PortalCommon::Window) {
        ToplevelInfo *toplevel = selectedToplevel();
        if (!toplevel || !m_context->foreignToplevelImageCaptureSourceManagerActive()) {
            return;
        }

        m_source = m_context->m_foreignToplevelImageCaptureSourceManager->create_source(toplevel->handle->object());
    } else {
        return;
    }

    if (!m_source) {
        return;
    }

    m_sourceState.session = new ImageCopyCaptureSession(
            m_context->m_imageCopyCaptureManager->create_session(
                    m_source, imageCopyCaptureOptions(m_preferDmaBuf && m_showCursor)),
            this);
    connect(m_sourceState.session, &ImageCopyCaptureSession::bufferSizeChanged,
            this, &ScreenCastPreview::handleSessionBufferSizeChanged);
    connect(m_sourceState.session, &ImageCopyCaptureSession::shmFormatChanged,
            this, &ScreenCastPreview::handleSessionShmFormatChanged);
    connect(m_sourceState.session, &ImageCopyCaptureSession::dmabufDeviceChanged,
            this, &ScreenCastPreview::handleSessionDmaBufDeviceChanged);
    connect(m_sourceState.session, &ImageCopyCaptureSession::dmabufFormatChanged,
            this, &ScreenCastPreview::handleSessionDmaBufFormatChanged);
    connect(m_sourceState.session, &ImageCopyCaptureSession::done,
            this, &ScreenCastPreview::handleSessionDone);
    connect(m_sourceState.session, &ImageCopyCaptureSession::stopped,
            this, &ScreenCastPreview::handleSessionStopped);

    wl_display_roundtrip(waylandDisplay()->wl_display());
}

void ScreenCastPreview::initializeCursorSession()
{
    if (!m_showCursor || m_preferDmaBuf || !m_source || !m_context || !m_context->imageCopyCaptureManagerActive()) {
        return;
    }

    auto *waylandApp = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!waylandApp || !waylandApp->pointer()) {
        return;
    }

    m_cursorSession = new ImageCopyCaptureCursorSession(
            m_context->m_imageCopyCaptureManager->create_pointer_cursor_session(m_source, waylandApp->pointer()),
            this);
    connect(m_cursorSession, &ImageCopyCaptureCursorSession::enter,
            this, &ScreenCastPreview::handleCursorEnter);
    connect(m_cursorSession, &ImageCopyCaptureCursorSession::leave,
            this, &ScreenCastPreview::handleCursorLeave);
    connect(m_cursorSession, &ImageCopyCaptureCursorSession::positionChanged,
            this, &ScreenCastPreview::handleCursorPositionChanged);
    connect(m_cursorSession, &ImageCopyCaptureCursorSession::hotspotChanged,
            this, &ScreenCastPreview::handleCursorHotspotChanged);

    m_cursorState.session = new ImageCopyCaptureSession(m_cursorSession->captureSession(), this);
    connect(m_cursorState.session, &ImageCopyCaptureSession::bufferSizeChanged,
            this, &ScreenCastPreview::handleSessionBufferSizeChanged);
    connect(m_cursorState.session, &ImageCopyCaptureSession::shmFormatChanged,
            this, &ScreenCastPreview::handleSessionShmFormatChanged);
    connect(m_cursorState.session, &ImageCopyCaptureSession::done,
            this, &ScreenCastPreview::handleSessionDone);
    connect(m_cursorState.session, &ImageCopyCaptureSession::stopped,
            this, &ScreenCastPreview::handleSessionStopped);

    wl_display_roundtrip(waylandDisplay()->wl_display());
}

void ScreenCastPreview::destroySession(CaptureState &state)
{
    if (state.frame) {
        state.frame->destroy();
        state.frame->deleteLater();
        state.frame = nullptr;
    }
    if (state.session) {
        state.session->destroy();
        state.session->deleteLater();
        state.session = nullptr;
    }
    state.shmFormats.clear();
    state.dmaBufFormats.clear();
    state.size = QSize();
    state.transform = 0;
    state.stopped = false;
}

void ScreenCastPreview::captureNext(CaptureState &state)
{
    if (!state.session || state.frame || state.stopped) {
        return;
    }

    state.frame = new ImageCopyCaptureFrame(state.session->create_frame(), this);
    if (m_usingDmaBuf && state.kind == CaptureKind::Source) {
        PreviewDmaBufBuffer *buffer = acquireDmaBufBuffer(state);
        if (!buffer) {
            state.frame->destroy();
            delete state.frame;
            state.frame = nullptr;
            return;
        }
        buffer->busy = true;
        state.frame->setProperty("previewDmaBuf", QVariant::fromValue<quintptr>(quintptr(buffer)));
        state.frame->attach_buffer(buffer->buffer);
    } else {
        PreviewShmBuffer *buffer = acquireBuffer(state);
        if (!buffer) {
            state.frame->destroy();
            delete state.frame;
            state.frame = nullptr;
            return;
        }
        buffer->busy = true;
        state.frame->setProperty("previewBuffer", QVariant::fromValue<quintptr>(quintptr(buffer)));
        state.frame->attach_buffer(buffer->buffer);
    }
    connect(state.frame, &ImageCopyCaptureFrame::transformChanged, this, [this, &state](uint32_t transform) {
        state.transform = transform;
    });
    connect(state.frame, &ImageCopyCaptureFrame::ready, this, &ScreenCastPreview::handleFrameReady);
    connect(state.frame, &ImageCopyCaptureFrame::failed, this, &ScreenCastPreview::handleFrameFailed);
    state.frame->capture();
}

PreviewDmaBufBuffer *ScreenCastPreview::acquireDmaBufBuffer(CaptureState &state) const
{
    Q_UNUSED(state)
    for (auto *buffer : m_dmaBufBuffers) {
        if (!buffer->busy)
            return buffer;
    }
    return nullptr;
}

void ScreenCastPreview::ensureBuffers(CaptureState &state)
{
    QList<PreviewShmBuffer *> &buffers = state.kind == CaptureKind::Cursor ? m_cursorBuffers : m_sourceBuffers;
    if (!buffers.isEmpty()
            && buffers.constFirst()->size == state.size
            && buffers.constFirst()->format == state.shmFormat) {
        return;
    }

    releaseBuffers(buffers);
    for (int i = 0; i < 2; ++i) {
        PreviewShmBuffer *buffer = createPreviewBuffer(m_context, state.size, state.shmFormat);
        if (!buffer) {
            break;
        }
        buffers.append(buffer);
    }
}

PreviewShmBuffer *ScreenCastPreview::acquireBuffer(CaptureState &state) const
{
    const QList<PreviewShmBuffer *> &buffers = state.kind == CaptureKind::Cursor ? m_cursorBuffers : m_sourceBuffers;
    for (PreviewShmBuffer *buffer : buffers) {
        if (!buffer->busy) {
            return buffer;
        }
    }
    return nullptr;
}

void ScreenCastPreview::releaseBuffers(QList<PreviewShmBuffer *> &buffers)
{
    qDeleteAll(buffers);
    buffers.clear();
}

void ScreenCastPreview::processFrame(CaptureState &state)
{
    if (!state.frame) {
        return;
    }

    auto *dmaBuf = reinterpret_cast<PreviewDmaBufBuffer *>(
            state.frame->property("previewDmaBuf").value<quintptr>());
    if (dmaBuf) {
        m_dmaBufFrame = {
            dmaBuf->fd, dmaBuf->size, dmaBuf->format, dmaBuf->stride,
            dmaBuf->offset, dmaBuf->modifier, state.transform,
            quintptr(dmaBuf), ++m_dmaBufSerial
        };
        state.frame->destroy();
        state.frame->deleteLater();
        state.frame = nullptr;
        Q_EMIT frameChanged();
        return;
    }

    PreviewShmBuffer *currentBuffer = reinterpret_cast<PreviewShmBuffer *>(
            state.frame->property("previewBuffer").value<quintptr>());

    if (!currentBuffer || !currentBuffer->data) {
        state.frame->destroy();
        state.frame->deleteLater();
        state.frame = nullptr;
        return;
    }

    const QImage image(static_cast<uchar *>(currentBuffer->data),
                       currentBuffer->size.width(),
                       currentBuffer->size.height(),
                       currentBuffer->stride,
                       toImageFormat(currentBuffer->format));
    const QImage copy = transformImage(image.copy(), state.transform);
    if (state.kind == CaptureKind::Cursor) {
        m_cursorImage = copy;
    } else {
        m_sourceImage = copy;
    }

    currentBuffer->busy = false;
    state.frame->destroy();
    state.frame->deleteLater();
    state.frame = nullptr;
    updateCompositedFrame();
}

void ScreenCastPreview::releaseDmaBufFrame(quintptr token)
{
    for (auto *buffer : std::as_const(m_dmaBufBuffers)) {
        if (quintptr(buffer) == token) {
            buffer->busy = false;
            captureNext(m_sourceState);
            return;
        }
    }
}

void ScreenCastPreview::fallbackToShm()
{
    if (!m_preferDmaBuf)
        return;
    m_preferDmaBuf = false;
    restart();
}

void ScreenCastPreview::updateCompositedFrame()
{
    if (m_sourceImage.isNull()) {
        if (!m_frame.isNull()) {
            m_frame = QImage();
            ++m_frameSerial;
            Q_EMIT frameChanged();
        }
        return;
    }

    QImage composited = m_sourceImage.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (m_showCursor && m_cursorVisible && !m_cursorImage.isNull()) {
        QPainter painter(&composited);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(m_cursorPosition - m_cursorHotspot,
                          m_cursorImage.convertToFormat(QImage::Format_RGBA8888_Premultiplied));
    }

    m_frame = composited;
    ++m_frameSerial;
    Q_EMIT frameChanged();
}

QImage ScreenCastPreview::transformImage(const QImage &image, uint32_t transform) const
{
    QTransform matrix;
    switch (transform) {
    case WL_OUTPUT_TRANSFORM_90:
        matrix.rotate(90);
        break;
    case WL_OUTPUT_TRANSFORM_180:
        matrix.rotate(180);
        break;
    case WL_OUTPUT_TRANSFORM_270:
        matrix.rotate(270);
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED:
        matrix.scale(-1, 1);
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        matrix.scale(-1, 1);
        matrix.rotate(90);
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
        matrix.scale(-1, 1);
        matrix.rotate(180);
        break;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        matrix.scale(-1, 1);
        matrix.rotate(270);
        break;
    default:
        break;
    }

    return matrix.isIdentity() ? image : image.transformed(matrix);
}

QScreen *ScreenCastPreview::selectedOutput() const
{
    if (!m_outputsModel || m_outputIndex < 0) {
        return nullptr;
    }
    return m_outputsModel->outputAt(m_outputIndex);
}

ToplevelInfo *ScreenCastPreview::selectedToplevel() const
{
    if (!m_toplevelsModel || m_toplevelIndex < 0) {
        return nullptr;
    }
    return m_toplevelsModel->toplevelAt(m_toplevelIndex);
}

bool ScreenCastPreview::isSupportedShmFormat(uint32_t format)
{
    return format == WL_SHM_FORMAT_XRGB8888 || format == WL_SHM_FORMAT_ARGB8888;
}

QImage::Format ScreenCastPreview::toImageFormat(wl_shm_format format)
{
    switch (format) {
    case WL_SHM_FORMAT_XRGB8888:
        return QImage::Format_RGB32;
    case WL_SHM_FORMAT_ARGB8888:
        return QImage::Format_ARGB32;
    default:
        return QImage::Format_Invalid;
    }
}
