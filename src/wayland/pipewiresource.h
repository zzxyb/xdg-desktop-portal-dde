// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "wayland-wayland-client-protocol.h"

#include "portalcommon.h"
#include "screencastcontext.h"

struct ScreenCopyFrameInfo;

struct PipeWireSource {
    PipeWireSource(enum PortalCommon::BufferType bufferType,
                   ScreenCopyFrameInfo *frameInfo,
                   QPointer<ScreenCastContext> context,
                   uint64_t modifier);
    ~PipeWireSource();

    wl_buffer *createWLSHMBuffer(QPointer<ScreenCastContext> context,
                                 int fd,
                                 enum wl_shm_format fmt,
                                 int width,
                                 int height,
                                 int stride);

    enum PortalCommon::BufferType bufferType;

    uint32_t width;
    uint32_t height;
    uint32_t format;
    int planeCount;

    int fd[4];
    uint32_t size[4];
    uint32_t stride[4];
    uint32_t offset[4];

    struct gbm_bo *bo = nullptr;
    struct wl_buffer *buffer = nullptr;
};
