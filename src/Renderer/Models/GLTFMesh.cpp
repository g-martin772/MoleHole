#include "GLTFMesh.h"
#include <tiny_gltf.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/type_ptr.hpp>
#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"

GLTFPrimitive::GLTFPrimitive()
    : m_indexCount(0), m_indexType(0),
      m_indexOffsetBytes(0), m_baseVertex(0), m_materialIndex(-1) {}

GLTFPrimitive::~GLTFPrimitive() {
    Cleanup();
}

GLTFPrimitive::GLTFPrimitive(GLTFPrimitive&& other) noexcept
    : m_VertexBuffer(std::move(other.m_VertexBuffer)),
      m_IndexBuffer(std::move(other.m_IndexBuffer)),
      m_indexCount(other.m_indexCount), m_indexType(other.m_indexType),
      m_indexOffsetBytes(other.m_indexOffsetBytes), m_baseVertex(other.m_baseVertex),
      m_materialIndex(other.m_materialIndex) {
    other.m_indexCount = 0;
    other.m_indexType = 0;
    other.m_indexOffsetBytes = 0;
    other.m_baseVertex = 0;
    other.m_materialIndex = -1;
}

GLTFPrimitive& GLTFPrimitive::operator=(GLTFPrimitive&& other) noexcept {
    if (this != &other) {
        Cleanup();
        m_VertexBuffer = std::move(other.m_VertexBuffer);
        m_IndexBuffer = std::move(other.m_IndexBuffer);
        m_indexCount = other.m_indexCount;
        m_indexType = other.m_indexType;
        m_indexOffsetBytes = other.m_indexOffsetBytes;
        m_baseVertex = other.m_baseVertex;
        m_materialIndex = other.m_materialIndex;

        other.m_indexCount = 0;
        other.m_indexType = 0;
        other.m_indexOffsetBytes = 0;
        other.m_baseVertex = 0;
        other.m_materialIndex = -1;
    }
    return *this;
}

void GLTFPrimitive::Cleanup() {
    m_VertexBuffer.reset();
    m_IndexBuffer.reset();
}

GLTFMaterial::GLTFMaterial()
    : m_baseColorFactor(1.0f), m_metallicFactor(1.0f), m_roughnessFactor(1.0f),
      m_hasBaseColorTexture(false), m_hasTransparency(false) {}

GLTFMesh::GLTFMesh() : m_position(0.0f), m_rotation(glm::identity<glm::quat>()), m_scale(1.0f) {}

GLTFMesh::~GLTFMesh() {
    Cleanup();
}

void GLTFMesh::Cleanup() {
    m_primitives.clear();
    m_materials.clear();
    if (m_DescriptorPool) {
        m_Device->GetDevice().destroyDescriptorPool(m_DescriptorPool);
        m_DescriptorPool = nullptr;
    }
}

bool GLTFMesh::Load(const std::string& path, VulkanDevice* device, vk::DescriptorSetLayout textureLayout) {
    m_Device = device;
    m_TextureLayout = textureLayout;
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ret = false;
    if (path.ends_with(".gltf")) {
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    } else if (path.ends_with(".glb")) {
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    } else {
        spdlog::error("Unsupported file format: {}", path);
        return false;
    }

    if (!warn.empty()) spdlog::warn("GLTF Warning: {}", warn);
    if (!err.empty()) {
        spdlog::error("GLTF Error: {}", err);
        return false;
    }
    if (!ret) {
        spdlog::error("Failed to load GLTF: {}", path);
        return false;
    }

    LoadMaterials(model);

    // Create Descriptor Pool for materials
    if (!m_materials.empty()) {
        std::vector<vk::DescriptorPoolSize> poolSizes = {
            { vk::DescriptorType::eCombinedImageSampler, static_cast<uint32_t>(m_materials.size()) }
        };
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = static_cast<uint32_t>(m_materials.size());
        
        m_DescriptorPool = m_Device->GetDevice().createDescriptorPool(poolInfo);

        // Allocate and Update Descriptor Sets
        for (auto& mat : m_materials) {
            if (mat.m_hasBaseColorTexture && mat.m_baseColorTexture) {
                vk::DescriptorSetAllocateInfo allocInfo{};
                allocInfo.descriptorPool = m_DescriptorPool;
                allocInfo.descriptorSetCount = 1;
                allocInfo.pSetLayouts = &m_TextureLayout;
                
                auto sets = m_Device->GetDevice().allocateDescriptorSets(allocInfo);
                mat.m_descriptorSet = sets[0];

                vk::DescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                imageInfo.imageView = mat.m_baseColorTexture->GetImageView();
                imageInfo.sampler = mat.m_baseColorTexture->GetSampler();

                vk::WriteDescriptorSet descriptorWrite{};
                descriptorWrite.dstSet = mat.m_descriptorSet;
                descriptorWrite.dstBinding = 0;
                descriptorWrite.dstArrayElement = 0;
                descriptorWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.pImageInfo = &imageInfo;

                m_Device->GetDevice().updateDescriptorSets(1, &descriptorWrite, 0, nullptr);
            }
        }
    }

    const tinygltf::Scene& scene = model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];
    for (int nodeIdx : scene.nodes) {
        ProcessNode(model, model.nodes[nodeIdx], glm::mat4(1.0f));
    }

    m_path = path;
    m_loaded = true;
    spdlog::info("Successfully loaded GLTF mesh: {}", path);
    return true;
}

void GLTFMesh::ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node, const glm::mat4& parentTransform) {
    glm::mat4 localTransform(1.0f);
    if (node.matrix.size() == 16) {
        localTransform = glm::make_mat4(node.matrix.data());
    } else {
        if (node.translation.size() == 3)
            localTransform = glm::translate(localTransform, glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
        if (node.rotation.size() == 4) {
            glm::quat rot(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
            localTransform *= glm::mat4_cast(rot);
        }
        if (node.scale.size() == 3)
            localTransform = glm::scale(localTransform, glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
    }

    glm::mat4 transform = parentTransform * localTransform;

    if (node.mesh >= 0) {
        ProcessMesh(model, node.mesh, transform);
    }

    for (int childIdx : node.children) {
        ProcessNode(model, model.nodes[childIdx], transform);
    }
}

void GLTFMesh::ProcessMesh(const tinygltf::Model& model, int meshIndex, const glm::mat4& transform) {
    const tinygltf::Mesh& mesh = model.meshes[meshIndex];

    for (const auto& primitive : mesh.primitives) {
        if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;

        GLTFPrimitive prim;
        prim.m_materialIndex = primitive.material;

        const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
        const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
        const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

        prim.m_indexCount = indexAccessor.count;
        prim.m_indexType = indexAccessor.componentType; // 5123 (USHORT) or 5125 (UINT)

        std::vector<uint32_t> indices;
        // Extract indices
        const void* indexDataPtr = &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];
        if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            const uint16_t* buf = static_cast<const uint16_t*>(indexDataPtr);
            for (size_t i = 0; i < indexAccessor.count; ++i) indices.push_back(buf[i]);
        } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            const uint32_t* buf = static_cast<const uint32_t*>(indexDataPtr);
            for (size_t i = 0; i < indexAccessor.count; ++i) indices.push_back(buf[i]);
        } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
             const uint8_t* buf = static_cast<const uint8_t*>(indexDataPtr);
             for (size_t i = 0; i < indexAccessor.count; ++i) indices.push_back(buf[i]);
        }

        // Create Index Buffer
        vk::DeviceSize indexBufferSize = indices.size() * sizeof(uint32_t);
        VulkanBufferSpec indexSpec{};
        indexSpec.Size = indexBufferSize;
        indexSpec.Usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        indexSpec.MemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        
        prim.m_IndexBuffer = std::make_unique<VulkanBuffer>();
        prim.m_IndexBuffer->Create(indexSpec, m_Device);
        prim.m_IndexBuffer->Write(indices.data(), indexBufferSize);

        // Extract Vertex Attributes
        std::vector<float> vertexData;
        int posIdx = primitive.attributes.count("POSITION") ? primitive.attributes.at("POSITION") : -1;
        int normIdx = primitive.attributes.count("NORMAL") ? primitive.attributes.at("NORMAL") : -1;
        int uvIdx = primitive.attributes.count("TEXCOORD_0") ? primitive.attributes.at("TEXCOORD_0") : -1;

        if (posIdx < 0) continue;

        const tinygltf::Accessor& posAcc = model.accessors[posIdx];
        const tinygltf::BufferView& posView = model.bufferViews[posAcc.bufferView];
        const unsigned char* posData = &model.buffers[posView.buffer].data[posView.byteOffset + posAcc.byteOffset];
        int posStride = posAcc.ByteStride(posView);

        const unsigned char* normData = nullptr;
        int normStride = 0;
        if (normIdx >= 0) {
            const tinygltf::Accessor& normAcc = model.accessors[normIdx];
            const tinygltf::BufferView& normView = model.bufferViews[normAcc.bufferView];
            normData = &model.buffers[normView.buffer].data[normView.byteOffset + normAcc.byteOffset];
            normStride = normAcc.ByteStride(normView);
        }

        const unsigned char* uvData = nullptr;
        int uvStride = 0;
        if (uvIdx >= 0) {
            const tinygltf::Accessor& uvAcc = model.accessors[uvIdx];
            const tinygltf::BufferView& uvView = model.bufferViews[uvAcc.bufferView];
            uvData = &model.buffers[uvView.buffer].data[uvView.byteOffset + uvAcc.byteOffset];
            uvStride = uvAcc.ByteStride(uvView);
        }

        for (size_t i = 0; i < posAcc.count; i++) {
            const float* pos = reinterpret_cast<const float*>(posData + i * posStride);
            vertexData.push_back(pos[0]);
            vertexData.push_back(pos[1]);
            vertexData.push_back(pos[2]);

            if (normData) {
                const float* norm = reinterpret_cast<const float*>(normData + i * normStride);
                vertexData.push_back(norm[0]);
                vertexData.push_back(norm[1]);
                vertexData.push_back(norm[2]);
            } else {
                vertexData.push_back(0.0f); vertexData.push_back(0.0f); vertexData.push_back(1.0f);
            }

            if (uvData) {
                const float* uv = reinterpret_cast<const float*>(uvData + i * uvStride);
                vertexData.push_back(uv[0]);
                vertexData.push_back(uv[1]);
            } else {
                vertexData.push_back(0.0f); vertexData.push_back(0.0f);
            }
        }

        // Create Vertex Buffer
        vk::DeviceSize vertexBufferSize = vertexData.size() * sizeof(float);
        VulkanBufferSpec vertexSpec{};
        vertexSpec.Size = vertexBufferSize;
        vertexSpec.Usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
        vertexSpec.MemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;

        prim.m_VertexBuffer = std::make_unique<VulkanBuffer>();
        prim.m_VertexBuffer->Create(vertexSpec, m_Device);
        prim.m_VertexBuffer->Write(vertexData.data(), vertexBufferSize);
        
        m_primitives.push_back(std::move(prim));

        // Store in temp cache for physics geometry
        TempCachePrim cachePrim;
        cachePrim.materialIndex = primitive.material;
        cachePrim.indexType = indexAccessor.componentType;
        cachePrim.indexCount = indexAccessor.count;
        cachePrim.vertex = vertexData;
        
        // Convert indices to unsigned char format for caching
        for (const auto& idx : indices) {
            cachePrim.indices.push_back(static_cast<unsigned char>(idx & 0xFF));
            if (sizeof(uint32_t) > 1) {
                cachePrim.indices.push_back(static_cast<unsigned char>((idx >> 8) & 0xFF));
            }
            if (sizeof(uint32_t) > 2) {
                cachePrim.indices.push_back(static_cast<unsigned char>((idx >> 16) & 0xFF));
                cachePrim.indices.push_back(static_cast<unsigned char>((idx >> 24) & 0xFF));
            }
        }
        
        m_tempCache.push_back(cachePrim);
    }
}

void GLTFMesh::LoadMaterials(const tinygltf::Model& model) {
    for (const auto& mat : model.materials) {
        GLTFMaterial material;
        if (mat.values.find("baseColorFactor") != mat.values.end()) {
            material.m_baseColorFactor = glm::make_vec4(mat.values.at("baseColorFactor").ColorFactor().data());
        }
        if (mat.values.find("baseColorTexture") != mat.values.end()) {
            material.m_baseColorTexture = LoadTextureFromModel(model, mat.values.at("baseColorTexture").TextureIndex());
            material.m_hasBaseColorTexture = true;
        }
        m_materials.push_back(material);
    }
}

std::shared_ptr<VulkanImage> GLTFMesh::LoadTextureFromModel(const tinygltf::Model& model, int textureIndex) {
    const tinygltf::Texture& tex = model.textures[textureIndex];
    const tinygltf::Image& image = model.images[tex.source];

    VulkanImageSpec spec{};
    spec.Size = glm::vec3(image.width, image.height, 1.0f);
    spec.Format = vk::Format::eR8G8B8A8Unorm; // Assumes RGBA 8-bit
    spec.Tiling = vk::ImageTiling::eOptimal;
    spec.Usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
    spec.MemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
    spec.CreateView = true;
    spec.ViewAspectFlags = vk::ImageAspectFlagBits::eColor;
    
    // Auto-calculate mips? For now 1.
    spec.MipLevels = 1; 

    auto vulkanImage = std::make_shared<VulkanImage>();
    vulkanImage->Create(spec, m_Device);

    // Create Sampler
    VulkanSamplerSpec samplerSpec{};
    if (tex.sampler >= 0) {
        const tinygltf::Sampler& gltfSampler = model.samplers[tex.sampler];
        // Map GLTF sampler to Vulkan (simplified mapping)
        samplerSpec.MinFilter = (gltfSampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST) ? vk::Filter::eNearest : vk::Filter::eLinear;
        samplerSpec.MagFilter = (gltfSampler.magFilter == TINYGLTF_TEXTURE_FILTER_NEAREST) ? vk::Filter::eNearest : vk::Filter::eLinear;
        samplerSpec.AddressU = (gltfSampler.wrapS == TINYGLTF_TEXTURE_WRAP_REPEAT) ? vk::SamplerAddressMode::eRepeat : 
                               (gltfSampler.wrapS == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE) ? vk::SamplerAddressMode::eClampToEdge : vk::SamplerAddressMode::eMirroredRepeat;
        samplerSpec.AddressV = (gltfSampler.wrapT == TINYGLTF_TEXTURE_WRAP_REPEAT) ? vk::SamplerAddressMode::eRepeat : 
                               (gltfSampler.wrapT == TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE) ? vk::SamplerAddressMode::eClampToEdge : vk::SamplerAddressMode::eMirroredRepeat;
    }
    vulkanImage->CreateSampler(samplerSpec);

    // Prepare data
    std::vector<unsigned char> rgbaData;
    const unsigned char* bufferData = image.image.data();
    vk::DeviceSize imageSize = image.width * image.height * 4;

    if (image.component == 3) {
        rgbaData.reserve(image.width * image.height * 4);
        for (size_t i = 0; i < image.width * image.height; i++) {
            rgbaData.push_back(image.image[i * 3 + 0]);
            rgbaData.push_back(image.image[i * 3 + 1]);
            rgbaData.push_back(image.image[i * 3 + 2]);
            rgbaData.push_back(255);
        }
        bufferData = rgbaData.data();
    }

    // Upload image data using staging buffer
    VulkanBufferSpec stagingSpec{};
    stagingSpec.Size = imageSize;
    stagingSpec.Usage = vk::BufferUsageFlagBits::eTransferSrc;
    stagingSpec.MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    
    VulkanBuffer stagingBuffer;
    stagingBuffer.Create(stagingSpec, m_Device);
    stagingBuffer.Write(bufferData, imageSize);

    // Transition to TransferDst
    vulkanImage->TransitionLayout(
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal,
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer
    );

    // Copy buffer to image
    vulkanImage->CopyBufferToImage(
        stagingBuffer.GetBuffer(),
        static_cast<uint32_t>(image.width),
        static_cast<uint32_t>(image.height)
    );

    // Transition to ShaderReadOnlyOptimal
    vulkanImage->TransitionLayout(
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eFragmentShader
    );

    stagingBuffer.Destroy();

    return vulkanImage;
}

void GLTFMesh::Render(vk::CommandBuffer cmd, vk::PipelineLayout layout) {
    for (const auto& prim : m_primitives) {
        if (!prim.m_VertexBuffer || !prim.m_IndexBuffer) continue;

        vk::Buffer vertexBuffers[] = { prim.m_VertexBuffer->GetBuffer() };
        vk::DeviceSize offsets[] = { 0 };
        cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        cmd.bindIndexBuffer(prim.m_IndexBuffer->GetBuffer(), 0, vk::IndexType::eUint32);

        // Bind material textures if needed
        if (prim.m_materialIndex >= 0 && prim.m_materialIndex < m_materials.size()) {
             const auto& mat = m_materials[prim.m_materialIndex];
             if (mat.m_hasBaseColorTexture && mat.m_descriptorSet) {
                 cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 1, 1, &mat.m_descriptorSet, 0, nullptr);
             }
        }

        cmd.drawIndexed(prim.m_IndexBuffer->GetSize() / sizeof(uint32_t), 1, 0, 0, 0);
    }
}

GLTFMesh::PhysicsGeometry GLTFMesh::GetPhysicsGeometry() const {
    PhysicsGeometry geometry;

    for (const auto& cachePrim : m_tempCache) {
        // Convert vertex data to vec3 (position only)
        // Vertex data format: pos[0,1,2], norm[3,4,5], uv[6,7]
        // We take every 8 floats and extract the first 3 for position
        const int VERTEX_STRIDE = 8; // pos(3) + normal(3) + uv(2)
        
        for (size_t i = 0; i < cachePrim.vertex.size(); i += VERTEX_STRIDE) {
            glm::vec3 pos(
                cachePrim.vertex[i],
                cachePrim.vertex[i + 1],
                cachePrim.vertex[i + 2]
            );
            geometry.vertices.push_back(pos);
        }

        // Add indices from cache
        // Reconstruct uint32_t indices from stored unsigned char data
        for (size_t i = 0; i < cachePrim.indices.size(); i += 4) {
            uint32_t idx = 0;
            if (i < cachePrim.indices.size()) idx |= cachePrim.indices[i];
            if (i + 1 < cachePrim.indices.size()) idx |= (cachePrim.indices[i + 1] << 8);
            if (i + 2 < cachePrim.indices.size()) idx |= (cachePrim.indices[i + 2] << 16);
            if (i + 3 < cachePrim.indices.size()) idx |= (cachePrim.indices[i + 3] << 24);
            geometry.indices.push_back(idx);
        }
    }

    return geometry;
}
