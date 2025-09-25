#pragma once

#include <vulkan/vulkan.h>

#include <epoxy/egl.h>

#include <gbm.h>

#include <QList>
#include <QSize>
#include <QByteArray>

namespace DMAHelpers
{
struct DmaBufPlane {
    int fd;
    uint32_t offset;
    uint32_t stride;
};

struct DmaBufAttributes {
    int width = 0;
    int height = 0;
    uint32_t format = 0;
    uint64_t modifier = 0;

    QList<DmaBufPlane> planes;
};

QByteArray formatEGLError(GLenum err);
gbm_bo *importDmabuf(const DmaBufAttributes &dmabufAttribs,
                     uint32_t format,
                     const QSize &size,
                     gbm_device *gbmDevice);
EGLImage createImage(EGLDisplay display,
                     const DmaBufAttributes &attribs,
                     uint32_t format,
                     const QSize &size,
                     gbm_device *device);
VkImage createVulkanImage(VkDevice device,
                          VkPhysicalDevice physicalDevice,
                          const DmaBufAttributes &dmabufAttribs,
                          uint32_t format,
                          const QSize &size,
                          gbm_device *gbmDevice);
}
