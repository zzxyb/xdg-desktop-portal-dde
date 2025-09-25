#include "dmahelpers.h"
#include "loggings.h"

#include <drm_fourcc.h>

namespace DMAHelpers
{
#define ENUM_STRING(x) case x: return #x;

QByteArray formatEGLError(GLenum err)
{
    switch (err) {
        ENUM_STRING(EGL_SUCCESS)
        ENUM_STRING(EGL_BAD_DISPLAY)
        ENUM_STRING(EGL_BAD_CONTEXT)
        ENUM_STRING(EGL_BAD_PARAMETER)
        ENUM_STRING(EGL_BAD_MATCH)
        ENUM_STRING(EGL_BAD_ACCESS)
        ENUM_STRING(EGL_BAD_ALLOC)
        ENUM_STRING(EGL_BAD_CONFIG)
    default:
        return QByteArray("0x") + QByteArray::number(err, 16);
    }
}

EGLImage createImage(EGLDisplay display,
                     const DmaBufAttributes &dmabufAttribs,
                     uint32_t format,
                     const QSize &size,
                     gbm_device *gbmDevice)
{
    Q_ASSERT(!size.isEmpty());
    gbm_bo *imported = nullptr;
    if (gbmDevice) {
        gbm_import_fd_data importInfo = {static_cast<int>(dmabufAttribs.planes[0].fd),
                                          static_cast<uint32_t>(size.width()),
                                          static_cast<uint32_t>(size.height()),
                                          static_cast<uint32_t>(dmabufAttribs.planes[0].stride),
                                          GBM_BO_FORMAT_ARGB8888};
        imported = gbm_bo_import(gbmDevice, GBM_BO_IMPORT_FD, &importInfo, GBM_BO_USE_SCANOUT);
        if (!imported) {
            qCWarning(SCREENCAST) << "Failed to process buffer: Cannot import passed GBM fd - " << strerror(errno);
            return EGL_NO_IMAGE_KHR;
        }
    } else {
        return EGL_NO_IMAGE_KHR;
    }

    const bool hasModifiers = dmabufAttribs.modifier != DRM_FORMAT_MOD_INVALID;

    static int lastSize = 37;
    QList<EGLint> attribs;
    attribs.reserve(lastSize);
    attribs << EGL_WIDTH
            << size.width()
            << EGL_HEIGHT
            << size.height()
            << EGL_LINUX_DRM_FOURCC_EXT
            << EGLint(format)
            << EGL_DMA_BUF_PLANE0_FD_EXT
            << dmabufAttribs.planes[0].fd
            << EGL_DMA_BUF_PLANE0_OFFSET_EXT
            << EGLint(dmabufAttribs.planes[0].offset)
            << EGL_DMA_BUF_PLANE0_PITCH_EXT
            << EGLint(dmabufAttribs.planes[0].stride);

    if (hasModifiers) {
        attribs << EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT
                << EGLint(dmabufAttribs.modifier & 0xffffffff)
                << EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT
                << EGLint(dmabufAttribs.modifier >> 32);
    }

    if (dmabufAttribs.planes.count() > 1) {
        attribs << EGL_DMA_BUF_PLANE1_FD_EXT
                << dmabufAttribs.planes[1].fd
                << EGL_DMA_BUF_PLANE1_OFFSET_EXT
                << EGLint(dmabufAttribs.planes[1].offset)
                << EGL_DMA_BUF_PLANE1_PITCH_EXT
                << EGLint(dmabufAttribs.planes[1].stride);

        if (hasModifiers) {
            attribs << EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT
                    << EGLint(dmabufAttribs.modifier & 0xffffffff)
                    << EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT
                    << EGLint(dmabufAttribs.modifier >> 32);
        }
    }

    if (dmabufAttribs.planes.count() > 2) {
        attribs << EGL_DMA_BUF_PLANE2_FD_EXT
                << dmabufAttribs.planes[2].fd
                << EGL_DMA_BUF_PLANE2_OFFSET_EXT
                << EGLint(dmabufAttribs.planes[2].offset)
                << EGL_DMA_BUF_PLANE2_PITCH_EXT
                << EGLint(dmabufAttribs.planes[2].stride);

        if (hasModifiers) {
            attribs << EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT
                    << EGLint(dmabufAttribs.modifier & 0xffffffff)
                    << EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT
                    << EGLint(dmabufAttribs.modifier >> 32);
        }
    }

    if (dmabufAttribs.planes.count() > 3) {
        attribs << EGL_DMA_BUF_PLANE3_FD_EXT
                << dmabufAttribs.planes[3].fd
                << EGL_DMA_BUF_PLANE3_OFFSET_EXT
                << EGLint(dmabufAttribs.planes[3].offset)
                << EGL_DMA_BUF_PLANE3_PITCH_EXT
                << EGLint(dmabufAttribs.planes[3].stride);

        if (hasModifiers) {
            attribs << EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT
                    << EGLint(dmabufAttribs.modifier & 0xffffffff)
                    << EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT
                    << EGLint(dmabufAttribs.modifier >> 32);
        }
    }

    attribs << EGL_NONE;
    lastSize = attribs.size();

    static auto eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    EGLImage ret = eglCreateImageKHR(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, imported, attribs.data());
    if (ret == EGL_NO_IMAGE_KHR) {
        qCWarning(SCREENCAST) << "invalid image" << formatEGLError(eglGetError());
    }
    if (imported) {
        gbm_bo_destroy(imported);
    }
    return ret;
}

VkImage createVulkanImage(VkDevice device,
                          VkPhysicalDevice physicalDevice,
                          const DmaBufAttributes &dmabufAttribs,
                          uint32_t format,
                          const QSize &size,
                          gbm_device *gbmDevice)
{
    Q_ASSERT(!size.isEmpty());
    gbm_bo *imported = nullptr;
    if (gbmDevice) {
        gbm_import_fd_data importInfo = {static_cast<int>(dmabufAttribs.planes[0].fd),
                                          static_cast<uint32_t>(size.width()),
                                          static_cast<uint32_t>(size.height()),
                                          static_cast<uint32_t>(dmabufAttribs.planes[0].stride),
                                          GBM_BO_FORMAT_ARGB8888};
        imported = gbm_bo_import(gbmDevice, GBM_BO_IMPORT_FD, &importInfo, GBM_BO_USE_SCANOUT);
        if (!imported) {
            qCWarning(SCREENCAST) << "Failed to process buffer: Cannot import passed GBM fd - " << strerror(errno);
            return VK_NULL_HANDLE;
        }
    } else {
        return VK_NULL_HANDLE;
    }

    VkImage vkImage = VK_NULL_HANDLE;
    VkDeviceMemory vkMemory = VK_NULL_HANDLE;

    VkImportMemoryFdInfoKHR importMemoryFdInfo = {};
    importMemoryFdInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    importMemoryFdInfo.fd = dmabufAttribs.planes[0].fd;

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = static_cast<VkDeviceSize>(size.width() * size.height() * 4);
    allocInfo.pNext = &importMemoryFdInfo;

    VkExternalMemoryFdInfoKHR externalMemoryFdInfo = {};
    externalMemoryFdInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_FD_INFO_KHR;
    externalMemoryFdInfo.fd = importMemoryFdInfo.fd;

    allocInfo.pNext = &externalMemoryFdInfo;

    VkMemoryAllocateFlagsInfo allocFlagsInfo = {};
    allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    allocInfo.pNext = &allocFlagsInfo;

    VkImageCreateInfo imageCreateInfo = {};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = static_cast<VkFormat>(format);
    imageCreateInfo.extent.width = static_cast<uint32_t>(size.width());
    imageCreateInfo.extent.height = static_cast<uint32_t>(size.height());
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    imageCreateInfo.flags = 0;

    VkResult result = vkCreateImage(device, &imageCreateInfo, nullptr, &vkImage);
    if (result != VK_SUCCESS) {
        qCWarning(SCREENCAST) << "Failed to create Vulkan image: " << result;
        return VK_NULL_HANDLE;
    }

    VkBindImageMemoryInfo bindInfo = {};
    bindInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
    bindInfo.image = vkImage;
    bindInfo.memory = vkMemory;
    bindInfo.memoryOffset = 0;

    result = vkBindImageMemory(device, vkImage, vkMemory, 0);
    if (result != VK_SUCCESS) {
        qCWarning(SCREENCAST) << "Failed to bind Vulkan image memory: " << result;
        vkDestroyImage(device, vkImage, nullptr);
        return VK_NULL_HANDLE;
    }

    if (imported) {
        gbm_bo_destroy(imported);
    }

    return vkImage;
}

// gbm_bo *importDmabuf(const DmaBufAttributes &dmabufAttribs, uint32_t format, const QSize &size, gbm_device *gbmDevice)
// {
//     gbm_bo *imported = nullptr;
//     if (dmabufAttribs.modifier != DRM_FORMAT_MOD_INVALID) {
//         gbm_import_fd_modifier_data importInfo;
//         importInfo.width = dmabufAttribs.width;
//         importInfo.height = dmabufAttribs.height;
//         importInfo.format = dmabufAttribs.format;
//         importInfo.num_fds = dmabufAttribs.planes.count();
//         importInfo.modifier = dmabufAttribs.modifier;
//         for (int i = 0; i < dmabufAttribs.planes.count(); i++) {
//             importInfo.fds[i] = dmabufAttribs.planes[i].fd;
//             importInfo.offsets[i] = dmabufAttribs.planes[i].offset;
//             importInfo.strides[i] = dmabufAttribs.planes[i].stride;
//         }
//         imported = gbm_bo_import(gbmDevice, GBM_BO_IMPORT_FD, &importInfo, GBM_BO_USE_SCANOUT);
//         if (!imported) {
//             qCWarning(SCREENCAST) << "Failed to process buffer: Cannot import with modifier passed GBM fd - " << strerror(errno);
//             return nullptr;
//         }
//     } else {
//         gbm_import_fd_data importInfo = {static_cast<int>(dmabufAttribs.planes[0].fd),
//                                           static_cast<uint32_t>(size.width()),
//                                           static_cast<uint32_t>(size.height()),
//                                           static_cast<uint32_t>(dmabufAttribs.planes[0].stride),
//                                           dmabufAttribs.format};
//         imported = gbm_bo_import(gbmDevice, GBM_BO_IMPORT_FD, &importInfo, GBM_BO_USE_SCANOUT);
//         if (!imported) {
//             qCWarning(SCREENCAST) << "Failed to process buffer: Cannot import passed GBM fd - " << strerror(errno);
//             return nullptr;
//         }
//     }

//     return imported;
// }
gbm_bo *importDmabuf(const DmaBufAttributes &dmabufAttribs,
                     uint32_t format,
                     const QSize &size,
                     gbm_device *gbmDevice)
{
    gbm_bo *imported = nullptr;

    if (dmabufAttribs.modifier != DRM_FORMAT_MOD_INVALID) {
        gbm_import_fd_modifier_data importInfo = {};
        importInfo.width    = dmabufAttribs.width ? dmabufAttribs.width : size.width();
        importInfo.height   = dmabufAttribs.height ? dmabufAttribs.height : size.height();
        importInfo.format   = dmabufAttribs.format;
        importInfo.num_fds  = qMin(dmabufAttribs.planes.count(), GBM_MAX_PLANES);
        importInfo.modifier = dmabufAttribs.modifier;

        for (int i = 0; i < importInfo.num_fds; i++) {
            importInfo.fds[i]     = dmabufAttribs.planes[i].fd;
            importInfo.offsets[i] = dmabufAttribs.planes[i].offset;
            importInfo.strides[i] = dmabufAttribs.planes[i].stride;
        }

        imported = gbm_bo_import(gbmDevice, GBM_BO_IMPORT_FD_MODIFIER,
                                 &importInfo, GBM_BO_USE_SCANOUT);
        if (!imported) {
            qCWarning(SCREENCAST) << "Failed to import GBM fd with modifier:"
                                  << strerror(errno);
            return nullptr;
        }
    } else {
        gbm_import_fd_data importInfo = {
            static_cast<int>(dmabufAttribs.planes[0].fd),
            static_cast<uint32_t>(size.width()),
            static_cast<uint32_t>(size.height()),
            static_cast<uint32_t>(dmabufAttribs.planes[0].stride),
            dmabufAttribs.format
        };

        imported = gbm_bo_import(gbmDevice, GBM_BO_IMPORT_FD,
                                 &importInfo, GBM_BO_USE_SCANOUT);
        if (!imported) {
            qCWarning(SCREENCAST) << "Failed to import GBM fd:"
                                  << strerror(errno);
            return nullptr;
        }
    }

    return imported;
}

}
