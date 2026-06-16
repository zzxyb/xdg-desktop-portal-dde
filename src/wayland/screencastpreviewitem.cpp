// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencastpreviewitem.h"

#include <QFile>
#include <QMatrix4x4>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QVulkanFunctions>
#include <QVector2D>
#include <QVector4D>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>
#include <algorithm>
#include <fcntl.h>
#include <rhi/qshader.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <vulkan/vulkan.h>

namespace {

struct Vertex {
    QVector2D position;
    QVector2D uv;
};

struct UniformBlock {
    QMatrix4x4 matrix;
    QVector4D params;
};

static QShader loadShader(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

class ScreenCastPreviewRenderer final : public QQuickRhiItemRenderer
{
public:
    ~ScreenCastPreviewRenderer() override
    {
        const quintptr token = m_importedDmaBufToken;
        QPointer<ScreenCastPreview> preview = m_preview;
        delete m_vertexBuffer;
        delete m_uniformBuffer;
        delete m_sampler;
        delete m_texture;
        delete m_srb;
        delete m_pipeline;
        destroyNativeTexture();
        if (token && preview)
            QMetaObject::invokeMethod(preview, [preview, token] {
                if (preview)
                    preview->releaseDmaBufFrame(token);
            }, Qt::QueuedConnection);
    }

protected:
    void initialize(QRhiCommandBuffer *cb) override
    {
        Q_UNUSED(cb)

        if (m_initialized) {
            if (m_pipeline && m_pipeline->renderPassDescriptor() != renderTarget()->renderPassDescriptor()) {
                m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
                m_pipeline->create();
            }
            return;
        }

        m_vertexBuffer = rhi()->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kVertices));
        m_vertexBuffer->create();

        m_uniformBuffer = rhi()->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(UniformBlock));
        m_uniformBuffer->create();

        m_sampler = rhi()->newSampler(QRhiSampler::Linear,
                                      QRhiSampler::Linear,
                                      QRhiSampler::None,
                                      QRhiSampler::ClampToEdge,
                                      QRhiSampler::ClampToEdge);
        m_sampler->create();

        recreateTexture(QImage(1, 1, QImage::Format_RGBA8888_Premultiplied));

        m_pipeline = rhi()->newGraphicsPipeline();
        m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_pipeline->setCullMode(QRhiGraphicsPipeline::None);
        m_pipeline->setSampleCount(1);
        m_pipeline->setShaderStages({
                { QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/screencast/shaders/preview.vert.qsb")) },
                { QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/screencast/shaders/preview.frag.qsb")) }
        });

        QRhiVertexInputLayout inputLayout;
        inputLayout.setBindings({ QRhiVertexInputBinding(sizeof(Vertex)) });
        inputLayout.setAttributes({
                QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2, 0),
                QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2, sizeof(QVector2D))
        });
        m_pipeline->setVertexInputLayout(inputLayout);
        m_pipeline->setShaderResourceBindings(m_srb);
        m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        m_pipeline->create();

        m_initialized = true;
    }

    void synchronize(QQuickRhiItem *item) override
    {
        auto *previewItem = static_cast<ScreenCastPreviewItem *>(item);
        m_preview = previewItem->preview();
        const PreviewDmaBufFrame dmaBufFrame = m_preview->dmaBufFrame();
        if (dmaBufFrame.serial != m_pendingDmaBuf.serial)
            m_pendingDmaBuf = dmaBufFrame;
        const quint64 frameSerial = previewItem->preview()->frameSerial();
        if (frameSerial != m_pendingFrameSerial) {
            m_pendingImage = previewItem->preview()->frame();
            m_pendingFrameSerial = frameSerial;
            m_hasPendingFrame = true;
        }

        QSize imageSize = m_pendingDmaBuf.isValid()
                ? m_pendingDmaBuf.size : previewItem->preview()->frameSize();
        if (m_pendingDmaBuf.transform == WL_OUTPUT_TRANSFORM_90
            || m_pendingDmaBuf.transform == WL_OUTPUT_TRANSFORM_270
            || m_pendingDmaBuf.transform == WL_OUTPUT_TRANSFORM_FLIPPED_90
            || m_pendingDmaBuf.transform == WL_OUTPUT_TRANSFORM_FLIPPED_270) {
            imageSize.transpose();
        }
        const QSize targetSize = item->width() > 0 && item->height() > 0
                ? QSize(int(item->width()), int(item->height()))
                : QSize();

        QMatrix4x4 matrix = rhi()->clipSpaceCorrMatrix();
        if (imageSize.isValid() && targetSize.isValid()) {
            const float imageAspect = float(imageSize.width()) / float(imageSize.height());
            const float targetAspect = float(targetSize.width()) / float(targetSize.height());
            if (imageAspect > targetAspect) {
                matrix.scale(1.0f, targetAspect / imageAspect);
            } else {
                matrix.scale(imageAspect / targetAspect, 1.0f);
            }
        }
        m_uniforms.matrix = matrix;
        m_uniforms.params.setZ(m_pendingDmaBuf.isValid()
                               ? float(m_pendingDmaBuf.transform) : 0.0f);
    }

    void render(QRhiCommandBuffer *cb) override
    {
        if (!m_initialized || !m_pipeline || !m_texture) {
            return;
        }

        if (m_pendingDmaBuf.isValid() && m_pendingDmaBuf.serial != m_importedDmaBufSerial) {
            if (importDmaBuf(m_pendingDmaBuf, cb)) {
                m_hasPendingFrame = false;
            } else {
                m_importedDmaBufSerial = m_pendingDmaBuf.serial;
                QPointer<ScreenCastPreview> preview = m_preview;
                if (preview)
                    QMetaObject::invokeMethod(preview, &ScreenCastPreview::fallbackToShm, Qt::QueuedConnection);
            }
        }

        QImage uploadImage;
        if (m_hasPendingFrame) {
            if (m_pendingImage.isNull()) {
                uploadImage = QImage(1, 1, QImage::Format_RGBA8888_Premultiplied);
                uploadImage.fill(Qt::transparent);
            } else {
                uploadImage = m_pendingImage.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            }
            if (uploadImage.size() != m_textureSize) {
                recreateTexture(uploadImage);
            }
        }

        QRhiResourceUpdateBatch *resourceUpdates = rhi()->nextResourceUpdateBatch();

        if (!m_uploadedGeometry) {
            resourceUpdates->uploadStaticBuffer(m_vertexBuffer, kVertices);
            m_uploadedGeometry = true;
        }

        resourceUpdates->updateDynamicBuffer(m_uniformBuffer, 0, sizeof(UniformBlock), &m_uniforms);

        if (!uploadImage.isNull()) {
            resourceUpdates->uploadTexture(m_texture, uploadImage);
            m_hasPendingFrame = false;
        }

        cb->beginPass(renderTarget(), QColor(0, 0, 0, 0), { 1.0f, 0 }, resourceUpdates);
        cb->setGraphicsPipeline(m_pipeline);
        cb->setViewport(QRhiViewport(0, 0, float(renderTarget()->pixelSize().width()), float(renderTarget()->pixelSize().height())));
        cb->setShaderResources();
        const QRhiCommandBuffer::VertexInput vertexInput[] = {
            { m_vertexBuffer, 0 }
        };
        cb->setVertexInput(0, 1, vertexInput);
        cb->draw(4);
        cb->endPass();
    }

private:
    bool importDmaBuf(const PreviewDmaBufFrame &frame, QRhiCommandBuffer *cb)
    {
        if (rhi()->backend() == QRhi::Vulkan)
            return importVulkanDmaBuf(frame, cb);
        if (rhi()->backend() != QRhi::OpenGLES2)
            return false;
        auto *context = QOpenGLContext::currentContext();
        auto *eglContext = context ? context->nativeInterface<QNativeInterface::QEGLContext>() : nullptr;
        if (!eglContext)
            return false;

        auto createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
        auto imageTarget = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
                eglGetProcAddress("glEGLImageTargetTexture2DOES"));
        if (!createImage || !imageTarget)
            return false;

        auto queryModifiers = reinterpret_cast<PFNEGLQUERYDMABUFMODIFIERSEXTPROC>(
                eglGetProcAddress("eglQueryDmaBufModifiersEXT"));
        if (queryModifiers && frame.modifier != DRM_FORMAT_MOD_INVALID) {
            EGLint modifierCount = 0;
            if (!queryModifiers(eglContext->display(), frame.format, 0, nullptr, nullptr,
                                &modifierCount))
                return false;
            QList<EGLuint64KHR> modifiers(modifierCount);
            QList<EGLBoolean> externalOnly(modifierCount);
            if (!queryModifiers(eglContext->display(), frame.format, modifierCount,
                                modifiers.data(), externalOnly.data(), &modifierCount))
                return false;
            bool found = false;
            for (EGLint i = 0; i < modifierCount; ++i) {
                if (modifiers[i] == frame.modifier) {
                    found = true;
                    if (externalOnly[i] == EGL_TRUE)
                        return false;
                    break;
                }
            }
            if (!found)
                return false;
        }

        auto cached = std::find_if(m_glesTextures.begin(), m_glesTextures.end(),
                                   [&frame](const GlesTexture &texture) {
            return texture.token == frame.token;
        });
        if (cached != m_glesTextures.end()) {
            auto *rhiTexture = rhi()->newTexture(QRhiTexture::BGRA8, frame.size);
            if (!rhiTexture->createFrom({ cached->texture, 0 })) {
                delete rhiTexture;
                return false;
            }
            const quintptr oldToken = m_importedDmaBufToken;
            delete m_texture;
            m_texture = rhiTexture;
            m_textureSize = frame.size;
            m_importedDmaBufToken = frame.token;
            m_importedDmaBufSerial = frame.serial;
            recreateShaderResources();
            if (oldToken && oldToken != frame.token && m_preview)
                QMetaObject::invokeMethod(m_preview, [preview = m_preview, oldToken] {
                    if (preview)
                        preview->releaseDmaBufFrame(oldToken);
                }, Qt::QueuedConnection);
            return true;
        }

        QList<EGLint> attributes {
            EGL_WIDTH, frame.size.width(),
            EGL_HEIGHT, frame.size.height(),
            EGL_LINUX_DRM_FOURCC_EXT, EGLint(frame.format),
            EGL_DMA_BUF_PLANE0_FD_EXT, frame.fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGLint(frame.offset),
            EGL_DMA_BUF_PLANE0_PITCH_EXT, EGLint(frame.stride)
        };
        if (frame.modifier != DRM_FORMAT_MOD_INVALID) {
            attributes << EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT << EGLint(frame.modifier & 0xffffffff)
                       << EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT << EGLint(frame.modifier >> 32);
        }
        attributes << EGL_NONE;

        EGLImageKHR image = createImage(eglContext->display(), EGL_NO_CONTEXT,
                                        EGL_LINUX_DMA_BUF_EXT, nullptr, attributes.constData());
        if (image == EGL_NO_IMAGE_KHR)
            return false;

        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        imageTarget(GL_TEXTURE_2D, image);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (glGetError() != GL_NO_ERROR) {
            glDeleteTextures(1, &texture);
            eglDestroyImage(eglContext->display(), image);
            return false;
        }

        auto *rhiTexture = rhi()->newTexture(QRhiTexture::BGRA8, frame.size);
        if (!rhiTexture->createFrom({ texture, 0 })) {
            delete rhiTexture;
            glDeleteTextures(1, &texture);
            eglDestroyImage(eglContext->display(), image);
            return false;
        }

        const quintptr oldToken = m_importedDmaBufToken;
        delete m_texture;
        m_texture = rhiTexture;
        m_textureSize = frame.size;
        m_glesTextures.append({ frame.token, eglContext->display(), image, texture });
        m_importedDmaBufToken = frame.token;
        m_importedDmaBufSerial = frame.serial;
        recreateShaderResources();
        if (oldToken && oldToken != frame.token && m_preview)
            QMetaObject::invokeMethod(m_preview, [preview = m_preview, oldToken] {
                if (preview)
                    preview->releaseDmaBufFrame(oldToken);
            }, Qt::QueuedConnection);
        return true;
    }

    bool importVulkanDmaBuf(const PreviewDmaBufFrame &frame, QRhiCommandBuffer *cb)
    {
        const auto *handles = static_cast<const QRhiVulkanNativeHandles *>(rhi()->nativeHandles());
        if (!handles || !handles->physDev || !handles->dev)
            return false;
        QVulkanFunctions *instanceFunctions = handles->inst ? handles->inst->functions() : nullptr;
        QVulkanDeviceFunctions *vk = handles->inst ? handles->inst->deviceFunctions(handles->dev) : nullptr;
        if (!instanceFunctions || !vk)
            return false;

        uint32_t extensionCount = 0;
        instanceFunctions->vkEnumerateDeviceExtensionProperties(handles->physDev, nullptr, &extensionCount, nullptr);
        QList<VkExtensionProperties> extensions(extensionCount);
        instanceFunctions->vkEnumerateDeviceExtensionProperties(handles->physDev, nullptr, &extensionCount, extensions.data());
        const auto hasExtension = [&extensions](const char *name) {
            return std::any_of(extensions.cbegin(), extensions.cend(), [name](const auto &extension) {
                return qstrcmp(extension.extensionName, name) == 0;
            });
        };
        if (!hasExtension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME)
            || !hasExtension(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME)
            || !hasExtension(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME)
            || !hasExtension(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME))
            return false;

        const VkFormat vkFormat = frame.format == DRM_FORMAT_ARGB8888
                ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_B8G8R8A8_UNORM;

        VkDrmFormatModifierPropertiesListEXT modifierList {
            VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT
        };
        VkFormatProperties2 formatProperties { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &modifierList };
        instanceFunctions->vkGetPhysicalDeviceFormatProperties2(handles->physDev, vkFormat, &formatProperties);
        QList<VkDrmFormatModifierPropertiesEXT> modifierProperties(modifierList.drmFormatModifierCount);
        modifierList.pDrmFormatModifierProperties = modifierProperties.data();
        instanceFunctions->vkGetPhysicalDeviceFormatProperties2(handles->physDev, vkFormat, &formatProperties);
        const auto modifierIt = std::find_if(modifierProperties.cbegin(), modifierProperties.cend(),
                                             [&frame](const auto &properties) {
            return properties.drmFormatModifier == frame.modifier;
        });
        if (modifierIt == modifierProperties.cend()
            || modifierIt->drmFormatModifierPlaneCount != 1
            || !(modifierIt->drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
            return false;

        VkPhysicalDeviceImageDrmFormatModifierInfoEXT imageModifierInfo {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
            nullptr, frame.modifier, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr
        };
        VkPhysicalDeviceExternalImageFormatInfo externalFormatInfo {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
            &imageModifierInfo, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
        };
        VkPhysicalDeviceImageFormatInfo2 imageFormatInfo {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
            &externalFormatInfo, vkFormat, VK_IMAGE_TYPE_2D,
            VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, VK_IMAGE_USAGE_SAMPLED_BIT, 0
        };
        VkExternalImageFormatProperties externalImageProperties {
            VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES
        };
        VkImageFormatProperties2 imageFormatProperties {
            VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, &externalImageProperties
        };
        if (instanceFunctions->vkGetPhysicalDeviceImageFormatProperties2(
                    handles->physDev, &imageFormatInfo, &imageFormatProperties) != VK_SUCCESS
            || uint32_t(frame.size.width()) > imageFormatProperties.imageFormatProperties.maxExtent.width
            || uint32_t(frame.size.height()) > imageFormatProperties.imageFormatProperties.maxExtent.height
            || !(externalImageProperties.externalMemoryProperties.externalMemoryFeatures
                 & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
            return false;

        VkSubresourceLayout planeLayout {};
        planeLayout.offset = frame.offset;
        planeLayout.rowPitch = frame.stride;
        planeLayout.size = 0;

        VkImageDrmFormatModifierExplicitCreateInfoEXT modifierInfo {
            VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
            nullptr, frame.modifier, 1, &planeLayout
        };
        VkExternalMemoryImageCreateInfo externalInfo {
            VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            &modifierInfo, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
        };
        VkImageCreateInfo imageInfo { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.pNext = &externalInfo;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = vkFormat;
        imageInfo.extent = { uint32_t(frame.size.width()), uint32_t(frame.size.height()), 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VkImage image = VK_NULL_HANDLE;
        if (vk->vkCreateImage(handles->dev, &imageInfo, nullptr, &image) != VK_SUCCESS)
            return false;

        VkImageMemoryRequirementsInfo2 requirementsInfo {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, nullptr, image
        };
        VkMemoryRequirements2 requirements { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
        vk->vkGetImageMemoryRequirements2(handles->dev, &requirementsInfo, &requirements);
        auto getFdProperties = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
                instanceFunctions->vkGetDeviceProcAddr(handles->dev, "vkGetMemoryFdPropertiesKHR"));
        if (!getFdProperties) {
            vk->vkDestroyImage(handles->dev, image, nullptr);
            return false;
        }
        VkMemoryFdPropertiesKHR fdProperties { VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
        if (getFdProperties(handles->dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                            frame.fd, &fdProperties) != VK_SUCCESS) {
            vk->vkDestroyImage(handles->dev, image, nullptr);
            return false;
        }

        VkPhysicalDeviceMemoryProperties memoryProperties;
        instanceFunctions->vkGetPhysicalDeviceMemoryProperties(handles->physDev, &memoryProperties);
        const uint32_t compatibleTypes =
                requirements.memoryRequirements.memoryTypeBits & fdProperties.memoryTypeBits;
        uint32_t memoryType = UINT32_MAX;
        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
            if (compatibleTypes & (1u << i)) {
                memoryType = i;
                if (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                    break;
            }
        }
        if (memoryType == UINT32_MAX) {
            vk->vkDestroyImage(handles->dev, image, nullptr);
            return false;
        }

        const int importedFd = fcntl(frame.fd, F_DUPFD_CLOEXEC, 0);
        if (importedFd < 0) {
            vk->vkDestroyImage(handles->dev, image, nullptr);
            return false;
        }
        VkMemoryDedicatedAllocateInfo dedicatedInfo {
            VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, nullptr, image, VK_NULL_HANDLE
        };
        VkImportMemoryFdInfoKHR importInfo {
            VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, &dedicatedInfo,
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, importedFd
        };
        VkMemoryAllocateInfo allocateInfo {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &importInfo,
            requirements.memoryRequirements.size, memoryType
        };
        VkDeviceMemory memory = VK_NULL_HANDLE;
        if (vk->vkAllocateMemory(handles->dev, &allocateInfo, nullptr, &memory) != VK_SUCCESS) {
            close(importedFd);
            vk->vkDestroyImage(handles->dev, image, nullptr);
            return false;
        }
        VkBindImageMemoryInfo bindInfo {
            VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, nullptr, image, memory, 0
        };
        if (vk->vkBindImageMemory2(handles->dev, 1, &bindInfo) != VK_SUCCESS) {
            vk->vkFreeMemory(handles->dev, memory, nullptr);
            vk->vkDestroyImage(handles->dev, image, nullptr);
            return false;
        }

        const auto *cbHandles = static_cast<const QRhiVulkanCommandBufferNativeHandles *>(cb->nativeHandles());
        if (!cbHandles || !cbHandles->commandBuffer) {
            vk->vkFreeMemory(handles->dev, memory, nullptr);
            vk->vkDestroyImage(handles->dev, image, nullptr);
            return false;
        }
        VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT;
        barrier.dstQueueFamilyIndex = handles->gfxQueueFamilyIdx;
        barrier.image = image;
        barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vk->vkCmdPipelineBarrier(cbHandles->commandBuffer,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        auto *rhiTexture = rhi()->newTexture(QRhiTexture::BGRA8, frame.size);
        if (!rhiTexture->createFrom({ quint64(image), int(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) })) {
            delete rhiTexture;
            vk->vkFreeMemory(handles->dev, memory, nullptr);
            vk->vkDestroyImage(handles->dev, image, nullptr);
            return false;
        }

        const quintptr oldToken = m_importedDmaBufToken;
        delete m_texture;
        destroyNativeTexture();
        m_texture = rhiTexture;
        m_textureSize = frame.size;
        m_vkDevice = handles->dev;
        m_vkFunctions = vk;
        m_vkImage = image;
        m_vkMemory = memory;
        m_importedDmaBufToken = frame.token;
        m_importedDmaBufSerial = frame.serial;
        recreateShaderResources();
        if (oldToken && m_preview)
            QMetaObject::invokeMethod(m_preview, [preview = m_preview, oldToken] {
                if (preview)
                    preview->releaseDmaBufFrame(oldToken);
            }, Qt::QueuedConnection);
        return true;
    }

    void destroyNativeTexture()
    {
        for (const GlesTexture &texture : std::as_const(m_glesTextures)) {
            if (texture.texture)
                glDeleteTextures(1, &texture.texture);
            if (texture.image != EGL_NO_IMAGE_KHR && texture.display != EGL_NO_DISPLAY)
                eglDestroyImage(texture.display, texture.image);
        }
        m_glesTextures.clear();
        if (m_vkImage && m_vkDevice && m_vkFunctions)
            m_vkFunctions->vkDeviceWaitIdle(m_vkDevice);
        if (m_vkImage && m_vkDevice && m_vkFunctions)
            m_vkFunctions->vkDestroyImage(m_vkDevice, m_vkImage, nullptr);
        if (m_vkMemory && m_vkDevice && m_vkFunctions)
            m_vkFunctions->vkFreeMemory(m_vkDevice, m_vkMemory, nullptr);
        m_vkImage = VK_NULL_HANDLE;
        m_vkMemory = VK_NULL_HANDLE;
        m_vkDevice = VK_NULL_HANDLE;
        m_vkFunctions = nullptr;
    }

    void recreateShaderResources()
    {
        delete m_srb;
        m_srb = rhi()->newShaderResourceBindings();
        m_srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage, m_uniformBuffer),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_texture, m_sampler)
        });
        m_srb->create();
        if (m_pipeline) {
            m_pipeline->setShaderResourceBindings(m_srb);
            m_pipeline->create();
        }
    }

    void recreateTexture(const QImage &image)
    {
        delete m_texture;
        destroyNativeTexture();
        m_texture = rhi()->newTexture(QRhiTexture::RGBA8, image.size());
        m_texture->create();
        m_textureSize = image.size();

        recreateShaderResources();
    }

    inline static const Vertex kVertices[4] = {
        { QVector2D(-1.0f, -1.0f), QVector2D(0.0f, 1.0f) },
        { QVector2D( 1.0f, -1.0f), QVector2D(1.0f, 1.0f) },
        { QVector2D(-1.0f,  1.0f), QVector2D(0.0f, 0.0f) },
        { QVector2D( 1.0f,  1.0f), QVector2D(1.0f, 0.0f) },
    };

    QRhiBuffer *m_vertexBuffer = nullptr;
    QRhiBuffer *m_uniformBuffer = nullptr;
    QRhiSampler *m_sampler = nullptr;
    QRhiTexture *m_texture = nullptr;
    QRhiShaderResourceBindings *m_srb = nullptr;
    QRhiGraphicsPipeline *m_pipeline = nullptr;
    QImage m_pendingImage;
    quint64 m_pendingFrameSerial = 0;
    QSize m_textureSize;
    UniformBlock m_uniforms;
    bool m_initialized = false;
    bool m_uploadedGeometry = false;
    bool m_hasPendingFrame = false;
    QPointer<ScreenCastPreview> m_preview;
    PreviewDmaBufFrame m_pendingDmaBuf;
    quint64 m_importedDmaBufSerial = 0;
    quintptr m_importedDmaBufToken = 0;
    struct GlesTexture {
        quintptr token = 0;
        EGLDisplay display = EGL_NO_DISPLAY;
        EGLImageKHR image = EGL_NO_IMAGE_KHR;
        GLuint texture = 0;
    };
    QList<GlesTexture> m_glesTextures;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkImage m_vkImage = VK_NULL_HANDLE;
    VkDeviceMemory m_vkMemory = VK_NULL_HANDLE;
    QVulkanDeviceFunctions *m_vkFunctions = nullptr;
};

} // namespace

ScreenCastPreviewItem::ScreenCastPreviewItem(QQuickItem *parent)
    : QQuickRhiItem(parent)
    , m_preview(new ScreenCastPreview(this))
{
    m_preview->setPreferDmaBuf(true);
    connect(m_preview, &ScreenCastPreview::frameChanged, this, &QQuickItem::update);
}

ScreenCastPreviewItem::~ScreenCastPreviewItem() = default;

int ScreenCastPreviewItem::sourceType() const
{
    return m_preview->sourceType();
}

void ScreenCastPreviewItem::setSourceType(int sourceType)
{
    if (m_preview->sourceType() == sourceType) {
        return;
    }

    m_preview->setSourceType(sourceType);
    Q_EMIT sourceTypeChanged();
}

QObject *ScreenCastPreviewItem::outputsModel() const
{
    return m_preview->outputsModel();
}

void ScreenCastPreviewItem::setOutputsModel(QObject *model)
{
    if (m_preview->outputsModel() == model) {
        return;
    }

    m_preview->setOutputsModel(model);
    Q_EMIT outputsModelChanged();
}

int ScreenCastPreviewItem::outputIndex() const
{
    return m_preview->outputIndex();
}

void ScreenCastPreviewItem::setOutputIndex(int index)
{
    if (m_preview->outputIndex() == index) {
        return;
    }

    m_preview->setOutputIndex(index);
    Q_EMIT outputIndexChanged();
}

QObject *ScreenCastPreviewItem::toplevelsModel() const
{
    return m_preview->toplevelsModel();
}

void ScreenCastPreviewItem::setToplevelsModel(QObject *model)
{
    if (m_preview->toplevelsModel() == model) {
        return;
    }

    m_preview->setToplevelsModel(model);
    Q_EMIT toplevelsModelChanged();
}

int ScreenCastPreviewItem::toplevelIndex() const
{
    return m_preview->toplevelIndex();
}

void ScreenCastPreviewItem::setToplevelIndex(int index)
{
    if (m_preview->toplevelIndex() == index) {
        return;
    }

    m_preview->setToplevelIndex(index);
    Q_EMIT toplevelIndexChanged();
}

bool ScreenCastPreviewItem::showCursor() const
{
    return m_preview->showCursor();
}

void ScreenCastPreviewItem::setShowCursor(bool show)
{
    if (m_preview->showCursor() == show) {
        return;
    }

    m_preview->setShowCursor(show);
    Q_EMIT showCursorChanged();
}

ScreenCastPreview *ScreenCastPreviewItem::preview() const
{
    return m_preview;
}

QQuickRhiItemRenderer *ScreenCastPreviewItem::createRenderer()
{
    return new ScreenCastPreviewRenderer;
}
