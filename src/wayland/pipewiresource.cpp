// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "pipewiresource.h"
#include "pipewireutils.h"
#include "loggings.h"
#include "protocols/shmbuffer.h"

#include <drm_fourcc.h>
#include <fcntl.h>
#include <sys/mman.h>

static int anonymous_shm_open()
{
    char name[] = "/xdpw-shm-XXXXXX";
    int retries = 100;

    do {
        ScreenCastContext::randname(name + strlen(name) - 6);

        --retries;
        // shm_open guarantees that O_CLOEXEC is set
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);

    return -1;
}

PipeWireSource::PipeWireSource(PortalCommon::BufferType bufferType,
                               ScreenCopyFrameInfo *frameInfo,
                               QPointer<ScreenCastContext> context,
                               uint64_t modifier)
{
    width = frameInfo->width;
    height = frameInfo->height;
    format = frameInfo->format;
    bufferType = bufferType;

    switch (bufferType) {
    case PortalCommon::SHM:
        planeCount = 1;
        size[0] = frameInfo->size;
        stride[0] = frameInfo->stride;
        offset[0] = 0;
        fd[0] = anonymous_shm_open();
        if (fd[0] == -1) {
            qCCritical(SCREENCAST, "xdpw: unable to create anonymous filedescriptor");
            return;
        }

        if (ftruncate(fd[0], size[0]) < 0) {
            qCCritical(SCREENCAST, "unable to truncate filedescriptor");
            close(fd[0]);
            return;
        }

        buffer = createWLSHMBuffer(context,
                                   fd[0],
                                   PipeWireutils::wlShmFormatFromDRMFormat(frameInfo->format),
                                   frameInfo->width,
                                   frameInfo->height,
                                   frameInfo->stride);
        if (!buffer) {
            qCCritical(SCREENCAST, "unable to create wl_buffer");
            close(fd[0]);
            return;
        }
        break;
    case PortalCommon::DMABUF:;
        uint32_t flags = GBM_BO_USE_RENDERING;
        if (modifier != DRM_FORMAT_MOD_INVALID) {
            uint64_t *modifiers = (uint64_t*)&modifier;
            bo = gbm_bo_create_with_modifiers2(context->m_gbmDevice,
                                               frameInfo->width,
                                               frameInfo->height,
                                               frameInfo->format,
                                               modifiers,
                                               1,
                                               flags);
        } else {
            if (context->m_forceModLinear) {
                flags |= GBM_BO_USE_LINEAR;
            }
            bo = gbm_bo_create(context->m_gbmDevice,
                               frameInfo->width,
                               frameInfo->height,
                               frameInfo->format,
                               flags);
        }

        if (!bo && modifier == DRM_FORMAT_MOD_LINEAR) {
            bo = gbm_bo_create(context->m_gbmDevice,
                               frameInfo->width,
                               frameInfo->height,
                               frameInfo->format,
                               flags | GBM_BO_USE_LINEAR);
        }

        if (!bo) {
            qCCritical(SCREENCAST, "failed to create gbm_bo");
            return;
        }
        planeCount = gbm_bo_get_plane_count(bo);

        struct zwp_linux_buffer_params_v1 *params = context->m_linuxDmaBuf->create_params();
        if (!params) {
            qCCritical(SCREENCAST, "failed to create linux_buffer_params");
            gbm_bo_destroy(bo);
            return;
        }

        for (int plane = 0; plane < planeCount; plane++) {
            size[plane] = 0;
            stride[plane] = gbm_bo_get_stride_for_plane(bo, plane);
            offset[plane] = gbm_bo_get_offset(bo, plane);
            uint64_t mod = gbm_bo_get_modifier(bo);
            fd[plane] = gbm_bo_get_fd_for_plane(bo, plane);

            if (fd[plane] < 0) {
                qCCritical(SCREENCAST, "failed to get file descriptor");
                zwp_linux_buffer_params_v1_destroy(params);
                gbm_bo_destroy(bo);
                for (int plane_tmp = 0; plane_tmp < plane; plane_tmp++) {
                    close(fd[plane_tmp]);
                }

                return;
            }

            zwp_linux_buffer_params_v1_add(params,
                                           fd[plane],
                                           plane,
                                           offset[plane],
                                           stride[plane],
                                           mod >> 32, mod & 0xffffffff);
        }
        buffer = zwp_linux_buffer_params_v1_create_immed(params,
                                                         width,
                                                         height,
                                                         format,
                                                         0);
        zwp_linux_buffer_params_v1_destroy(params);

        if (!buffer) {
            qCCritical(SCREENCAST, "failed to create buffer");
            gbm_bo_destroy(bo);
            for (int plane = 0; plane < planeCount; plane++) {
                close(fd[plane]);
            }

            return;
        }
    }
}

PipeWireSource::~PipeWireSource()
{
    if (buffer)
        wl_buffer_destroy(buffer);

    if (bufferType == PortalCommon::DMABUF) {
        if (bo) {
            gbm_bo_destroy(bo);
        }
    }
    for (int plane = 0; plane < planeCount; plane++) {
        close(fd[plane]);
    }
}

wl_buffer *PipeWireSource::createWLSHMBuffer(QPointer<ScreenCastContext> context,
                                             int fd,
                                             wl_shm_format fmt,
                                             int width,
                                             int height,
                                             int stride)
{
    if (!context->m_shm) {
        qCCritical(SCREENCAST) << "error, WLShm is nullptr";
        return nullptr;
    }

    if (!context->m_shmInterfaceActive) {
        qCCritical(SCREENCAST) << "error, WLShm is deactive";
        return nullptr;
    }

    int size = stride * height;

    if (fd < 0) {
        qCCritical(SCREENCAST) << "error, fd < 0";
        return nullptr;
    }

    struct wl_shm_pool *pool = context->m_shm->create_pool(fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, fmt);
    wl_shm_pool_destroy(pool);

    return buffer;
}
