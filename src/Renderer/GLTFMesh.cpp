#include "GLTFMesh.h"
#include "Shader.h"
#include <glad/gl.h>
#include <tiny_gltf.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/type_ptr.hpp>
#include <Application/Profiler.h>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <cstring>

namespace {
struct MeshCacheHeader {
    char magic[8];      // "MHMESH\0"
    uint32_t version;   // 1
    uint64_t srcSize;   // bytes of source .gltf/.glb
    uint64_t srcMTimeNs; // last write time ns
    uint32_t primCount; // number of primitives
};

inline uint64_t FileSize(const std::filesystem::path& p) {
    std::error_code ec;
    auto sz = std::filesystem::file_size(p, ec);
    return ec ? 0ULL : static_cast<uint64_t>(sz);
}

inline uint64_t FileMTimeNs(const std::filesystem::path& p) {
    std::error_code ec;
    auto tp = std::filesystem::last_write_time(p, ec);
    if (ec) return 0ULL;
    return static_cast<uint64_t>(tp.time_since_epoch().count());
}
}

GLTFPrimitive::GLTFPrimitive()
        : m_VAO(0), m_VBO(0), m_EBO(0), m_indexCount(0), m_indexType(GL_UNSIGNED_INT),
            m_indexOffsetBytes(0), m_baseVertex(0), m_materialIndex(-1) {}

GLTFPrimitive::~GLTFPrimitive() {
    Cleanup();
}

GLTFPrimitive::GLTFPrimitive(GLTFPrimitive&& other) noexcept
        : m_VAO(other.m_VAO), m_VBO(other.m_VBO), m_EBO(other.m_EBO),
            m_indexCount(other.m_indexCount), m_indexType(other.m_indexType),
            m_indexOffsetBytes(other.m_indexOffsetBytes), m_baseVertex(other.m_baseVertex),
            m_materialIndex(other.m_materialIndex) {
    other.m_VAO = 0;
    other.m_VBO = 0;
    other.m_EBO = 0;
    other.m_indexCount = 0;
    other.m_indexType = GL_UNSIGNED_INT;
    other.m_indexOffsetBytes = 0;
    other.m_baseVertex = 0;
    other.m_materialIndex = -1;
}

GLTFPrimitive& GLTFPrimitive::operator=(GLTFPrimitive&& other) noexcept {
    if (this != &other) {
        Cleanup();
        m_VAO = other.m_VAO;
        m_VBO = other.m_VBO;
        m_EBO = other.m_EBO;
        m_indexCount = other.m_indexCount;
        m_indexType = other.m_indexType;
    m_indexOffsetBytes = other.m_indexOffsetBytes;
    m_baseVertex = other.m_baseVertex;
        m_materialIndex = other.m_materialIndex;

        other.m_VAO = 0;
        other.m_VBO = 0;
        other.m_EBO = 0;
        other.m_indexCount = 0;
        other.m_indexType = GL_UNSIGNED_INT;
        other.m_indexOffsetBytes = 0;
        other.m_baseVertex = 0;
        other.m_materialIndex = -1;
    }
    return *this;
}

void GLTFPrimitive::Cleanup() {
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO) glDeleteBuffers(1, &m_VBO);
    if (m_EBO) glDeleteBuffers(1, &m_EBO);
    m_VAO = m_VBO = m_EBO = 0;
}

GLTFMaterial::GLTFMaterial()
    : m_baseColorFactor(1.0f), m_metallicFactor(1.0f), m_roughnessFactor(1.0f),
      m_baseColorTexture(0), m_hasBaseColorTexture(false), m_hasTransparency(false) {}

GLTFMesh::GLTFMesh()
    : m_position(0.0f), m_rotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)),
      m_scale(1.0f), m_loaded(false) {}

GLTFMesh::~GLTFMesh() {
    Cleanup();
}

void GLTFMesh::Cleanup() {
    m_primitives.clear();
    for (auto& mat : m_materials) {
        if (mat.m_baseColorTexture) {
            glDeleteTextures(1, &mat.m_baseColorTexture);
        }
    }
    m_materials.clear();
    if (m_sharedVAO) glDeleteVertexArrays(1, &m_sharedVAO);
    if (m_sharedVBO) glDeleteBuffers(1, &m_sharedVBO);
    if (m_sharedEBO) glDeleteBuffers(1, &m_sharedEBO);
    m_sharedVAO = m_sharedVBO = m_sharedEBO = 0;
    m_useSharedBuffers = false;
    m_loaded = false;
}

bool GLTFMesh::Load(const std::string& path) {
    PROFILE_FUNCTION();
    Cleanup();

    // Try geometry cache first
    std::filesystem::path srcPath(path);
    std::filesystem::path cachePath = srcPath;
    cachePath += ".mhmesh";
    bool loadedGeometryFromCache = false;
    if (std::filesystem::exists(cachePath)) {
        std::ifstream in(cachePath, std::ios::binary);
        MeshCacheHeader hdr{};
        if (in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr))) {
            if (std::memcmp(hdr.magic, "MHMESH\0", 8) == 0 && hdr.version == 1) {
                if (hdr.srcSize == FileSize(srcPath) && hdr.srcMTimeNs == FileMTimeNs(srcPath)) {
                    std::vector<TempCachePrim> cachePrims;
                    cachePrims.reserve(hdr.primCount);
                    for (uint32_t i = 0; i < hdr.primCount; ++i) {
                        TempCachePrim cp;
                        int32_t materialIndex;
                        uint32_t indexType;
                        uint32_t indexCount;
                        uint32_t vertexFloatCount;
                        if (!in.read(reinterpret_cast<char*>(&materialIndex), sizeof(materialIndex))) break;
                        if (!in.read(reinterpret_cast<char*>(&indexType), sizeof(indexType))) break;
                        if (!in.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount))) break;
                        if (!in.read(reinterpret_cast<char*>(&vertexFloatCount), sizeof(vertexFloatCount))) break;

                        cp.materialIndex = materialIndex;
                        cp.indexType = indexType;
                        cp.indexCount = indexCount;
                        cp.vertex.resize(vertexFloatCount);
                        if (!in.read(reinterpret_cast<char*>(cp.vertex.data()), vertexFloatCount * sizeof(float))) break;

                        uint32_t indexByteCount;
                        if (!in.read(reinterpret_cast<char*>(&indexByteCount), sizeof(indexByteCount))) break;
                        cp.indices.resize(indexByteCount);
                        if (!in.read(reinterpret_cast<char*>(cp.indices.data()), indexByteCount)) break;

                        cachePrims.push_back(std::move(cp));
                    }

                    if (cachePrims.size() == hdr.primCount) {
                        // Build shared buffers
                        m_useSharedBuffers = true;
                        std::vector<float> allVertices;
                        std::vector<unsigned char> allIndices;
                        allVertices.reserve( std::accumulate(cachePrims.begin(), cachePrims.end(), size_t{0},
                            [](size_t s, const TempCachePrim& c){ return s + c.vertex.size(); }) );
                        allIndices.reserve( std::accumulate(cachePrims.begin(), cachePrims.end(), size_t{0},
                            [](size_t s, const TempCachePrim& c){ return s + c.indices.size(); }) );

                        m_primitives.clear();
                        m_primitives.reserve(cachePrims.size());

                        size_t vertexFloatsSoFar = 0; // floats count; 8 floats per vertex
                        size_t indexBytesSoFar = 0;
                        for (const auto& cp : cachePrims) {
                            GLTFPrimitive prim;
                            prim.m_materialIndex = cp.materialIndex;
                            prim.m_indexType = cp.indexType;
                            prim.m_indexCount = cp.indexCount;
                            prim.m_baseVertex = static_cast<int>(vertexFloatsSoFar / 8);
                            prim.m_indexOffsetBytes = static_cast<unsigned int>(indexBytesSoFar);

                            allVertices.insert(allVertices.end(), cp.vertex.begin(), cp.vertex.end());
                            allIndices.insert(allIndices.end(), cp.indices.begin(), cp.indices.end());

                            indexBytesSoFar += cp.indices.size();
                            vertexFloatsSoFar += cp.vertex.size();
                            m_primitives.push_back(std::move(prim));
                        }

                        glGenVertexArrays(1, &m_sharedVAO);
                        glGenBuffers(1, &m_sharedVBO);
                        glGenBuffers(1, &m_sharedEBO);

                        glBindVertexArray(m_sharedVAO);
                        glBindBuffer(GL_ARRAY_BUFFER, m_sharedVBO);
                        glBufferData(GL_ARRAY_BUFFER, allVertices.size() * sizeof(float), allVertices.data(), GL_STATIC_DRAW);
                        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_sharedEBO);
                        glBufferData(GL_ELEMENT_ARRAY_BUFFER, allIndices.size(), allIndices.data(), GL_STATIC_DRAW);

                        glEnableVertexAttribArray(0);
                        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
                        glEnableVertexAttribArray(1);
                        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
                        glEnableVertexAttribArray(2);
                        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
                        glBindVertexArray(0);

                        loadedGeometryFromCache = true;
                        spdlog::info("Loaded GLTF geometry from cache (shared buffers): {} ({} primitives)", path, hdr.primCount);
                    } else {
                        m_primitives.clear();
                    }
                }
            }
        }
    }

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

    if (!warn.empty()) {
        spdlog::warn("GLTF Warning: {}", warn);
    }

    if (!err.empty()) {
        spdlog::error("GLTF Error: {}", err);
        return false;
    }

    if (!ret) {
        spdlog::error("Failed to load GLTF: {}", path);
        return false;
    }

    LoadMaterials(model);

    if (!loadedGeometryFromCache) {
        const tinygltf::Scene& scene = model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];
        for (int nodeIdx : scene.nodes) {
            ProcessNode(model, model.nodes[nodeIdx], glm::mat4(1.0f));
        }
        // Save geometry cache
        if (!m_tempCache.empty()) {
            std::ofstream out(cachePath, std::ios::binary | std::ios::trunc);
            if (out) {
                MeshCacheHeader hdr{};
                std::memset(&hdr, 0, sizeof(hdr));
                std::memcpy(hdr.magic, "MHMESH\0", 8);
                hdr.version = 1;
                hdr.srcSize = FileSize(srcPath);
                hdr.srcMTimeNs = FileMTimeNs(srcPath);
                hdr.primCount = static_cast<uint32_t>(m_tempCache.size());
                out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

                for (const auto& cp : m_tempCache) {
                    int32_t materialIndex = cp.materialIndex;
                    uint32_t indexType = cp.indexType;
                    uint32_t indexCount = cp.indexCount;
                    uint32_t vertexFloatCount = static_cast<uint32_t>(cp.vertex.size());
                    out.write(reinterpret_cast<const char*>(&materialIndex), sizeof(materialIndex));
                    out.write(reinterpret_cast<const char*>(&indexType), sizeof(indexType));
                    out.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));
                    out.write(reinterpret_cast<const char*>(&vertexFloatCount), sizeof(vertexFloatCount));
                    out.write(reinterpret_cast<const char*>(cp.vertex.data()), cp.vertex.size() * sizeof(float));
                    uint32_t indexByteCount = static_cast<uint32_t>(cp.indices.size());
                    out.write(reinterpret_cast<const char*>(&indexByteCount), sizeof(indexByteCount));
                    out.write(reinterpret_cast<const char*>(cp.indices.data()), cp.indices.size());
                }
                spdlog::info("Wrote GLTF geometry cache: {} ({} primitives)", cachePath.string(), m_tempCache.size());
            }
            m_tempCache.clear();
        }
    }

    m_shader = std::make_unique<Shader>("../shaders/mesh.vert", "../shaders/mesh.frag");
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
        if (node.translation.size() == 3) {
            localTransform = glm::translate(localTransform,
                glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
        }
        if (node.rotation.size() == 4) {
            glm::quat rot(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
            localTransform *= glm::mat4_cast(rot);
        }
        if (node.scale.size() == 3) {
            localTransform = glm::scale(localTransform,
                glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
        }
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
    PROFILE_FUNCTION();
    const tinygltf::Mesh& mesh = model.meshes[meshIndex];

    for (const auto& primitive : mesh.primitives) {
        if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;

        GLTFPrimitive prim;
        prim.m_materialIndex = primitive.material;

        const tinygltf::Accessor& indexAccessor = model.accessors[primitive.indices];
        const tinygltf::BufferView& indexBufferView = model.bufferViews[indexAccessor.bufferView];
        const tinygltf::Buffer& indexBuffer = model.buffers[indexBufferView.buffer];

        prim.m_indexCount = indexAccessor.count;

    std::vector<float> vertexData;
        int positionAccessorIdx = primitive.attributes.count("POSITION") ? primitive.attributes.at("POSITION") : -1;
        int normalAccessorIdx = primitive.attributes.count("NORMAL") ? primitive.attributes.at("NORMAL") : -1;
        int texcoordAccessorIdx = primitive.attributes.count("TEXCOORD_0") ? primitive.attributes.at("TEXCOORD_0") : -1;

        if (positionAccessorIdx < 0) continue;

        const tinygltf::Accessor& posAccessor = model.accessors[positionAccessorIdx];
        const tinygltf::BufferView& posBufferView = model.bufferViews[posAccessor.bufferView];
        const tinygltf::Buffer& posBuffer = model.buffers[posBufferView.buffer];

        size_t posStride = posBufferView.byteStride ? posBufferView.byteStride : 3 * sizeof(float);
        const unsigned char* posData = &posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset];

        const unsigned char* normData = nullptr;
        size_t normStride = 0;
        if (normalAccessorIdx >= 0) {
            const tinygltf::Accessor& normAccessor = model.accessors[normalAccessorIdx];
            const tinygltf::BufferView& normBufferView = model.bufferViews[normAccessor.bufferView];
            const tinygltf::Buffer& normBuffer = model.buffers[normBufferView.buffer];
            normStride = normBufferView.byteStride ? normBufferView.byteStride : 3 * sizeof(float);
            normData = &normBuffer.data[normBufferView.byteOffset + normAccessor.byteOffset];
        }

        const unsigned char* texData = nullptr;
        size_t texStride = 0;
        if (texcoordAccessorIdx >= 0) {
            const tinygltf::Accessor& texAccessor = model.accessors[texcoordAccessorIdx];
            const tinygltf::BufferView& texBufferView = model.bufferViews[texAccessor.bufferView];
            const tinygltf::Buffer& texBuffer = model.buffers[texBufferView.buffer];
            texStride = texBufferView.byteStride ? texBufferView.byteStride : 2 * sizeof(float);
            texData = &texBuffer.data[texBufferView.byteOffset + texAccessor.byteOffset];
        }

        // SPEED
        vertexData.reserve(vertexData.size() + posAccessor.count * 8);

        for (size_t i = 0; i < posAccessor.count; ++i) {
            const float* positions = reinterpret_cast<const float*>(posData + i * posStride);
            // MORE SPEED
            if (transform == glm::mat4(1.0f)) {
                vertexData.push_back(positions[0]);
                vertexData.push_back(positions[1]);
                vertexData.push_back(positions[2]);
            } else {
                glm::vec3 pos(positions[0], positions[1], positions[2]);
                glm::vec4 transformedPos = transform * glm::vec4(pos, 1.0f);
                vertexData.push_back(transformedPos.x);
                vertexData.push_back(transformedPos.y);
                vertexData.push_back(transformedPos.z);
            }

            if (normData) {
                const float* normals = reinterpret_cast<const float*>(normData + i * normStride);
                if (transform == glm::mat4(1.0f)) {
                    vertexData.push_back(normals[0]);
                    vertexData.push_back(normals[1]);
                    vertexData.push_back(normals[2]);
                } else {
                    glm::vec3 normal(normals[0], normals[1], normals[2]);
                    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
                    glm::vec3 transformedNormal = glm::normalize(normalMatrix * normal);
                    vertexData.push_back(transformedNormal.x);
                    vertexData.push_back(transformedNormal.y);
                    vertexData.push_back(transformedNormal.z);
                }
            } else {
                vertexData.push_back(0.0f);
                vertexData.push_back(1.0f);
                vertexData.push_back(0.0f);
            }

            if (texData) {
                const float* texcoords = reinterpret_cast<const float*>(texData + i * texStride);
                vertexData.push_back(texcoords[0]);
                vertexData.push_back(1.0f - texcoords[1]);
            } else {
                vertexData.push_back(0.0f);
                vertexData.push_back(0.0f);
            }
        }

        glGenVertexArrays(1, &prim.m_VAO);
        glGenBuffers(1, &prim.m_VBO);
        glGenBuffers(1, &prim.m_EBO);

        glBindVertexArray(prim.m_VAO);

        glBindBuffer(GL_ARRAY_BUFFER, prim.m_VBO);
        glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.data(), GL_STATIC_DRAW);

        const unsigned char* indexData = &indexBuffer.data[indexBufferView.byteOffset + indexAccessor.byteOffset];

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prim.m_EBO);

        std::vector<unsigned char> indexBytes;
        indexBytes.reserve(indexAccessor.count * 4); // upper bound
        if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            prim.m_indexType = GL_UNSIGNED_SHORT;
            size_t bytes = indexAccessor.count * sizeof(unsigned short);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, bytes, indexData, GL_STATIC_DRAW);
            indexBytes.assign(indexData, indexData + bytes);
        } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            prim.m_indexType = GL_UNSIGNED_INT;
            size_t bytes = indexAccessor.count * sizeof(unsigned int);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, bytes, indexData, GL_STATIC_DRAW);
            indexBytes.assign(indexData, indexData + bytes);
        } else {
            // Fallback to 8-bit indices
            prim.m_indexType = GL_UNSIGNED_BYTE;
            size_t bytes = indexAccessor.count * sizeof(unsigned char);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, bytes, indexData, GL_STATIC_DRAW);
            indexBytes.assign(indexData, indexData + bytes);
        }

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

        glBindVertexArray(0);

        // Save CPU-side data for cache
        TempCachePrim cachePrim;
        cachePrim.materialIndex = prim.m_materialIndex;
        cachePrim.indexType = prim.m_indexType;
        cachePrim.indexCount = prim.m_indexCount;
        cachePrim.vertex = vertexData; // copy
        cachePrim.indices = std::move(indexBytes);
        m_tempCache.push_back(std::move(cachePrim));

        m_primitives.push_back(std::move(prim));
    }
}

void GLTFMesh::LoadMaterials(const tinygltf::Model& model) {
    PROFILE_FUNCTION();
    for (const auto& mat : model.materials) {
        GLTFMaterial material;

        if (mat.pbrMetallicRoughness.baseColorFactor.size() == 4) {
            material.m_baseColorFactor = glm::vec4(
                mat.pbrMetallicRoughness.baseColorFactor[0],
                mat.pbrMetallicRoughness.baseColorFactor[1],
                mat.pbrMetallicRoughness.baseColorFactor[2],
                mat.pbrMetallicRoughness.baseColorFactor[3]
            );
        }

        material.m_metallicFactor = mat.pbrMetallicRoughness.metallicFactor;
        material.m_roughnessFactor = mat.pbrMetallicRoughness.roughnessFactor;

        material.m_hasTransparency = false;

        if (mat.alphaMode == "BLEND") {
            material.m_hasTransparency = true;
        } else if (mat.alphaMode == "MASK") {
            material.m_hasTransparency = false;
        }

        if (material.m_baseColorFactor.a < 0.99f) {
            material.m_hasTransparency = true;
        }

        if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
            int texIndex = mat.pbrMetallicRoughness.baseColorTexture.index;
            material.m_baseColorTexture = LoadTextureFromModel(model, texIndex);
            material.m_hasBaseColorTexture = true;
        }

        m_materials.push_back(material);
    }
}

unsigned int GLTFMesh::LoadTextureFromModel(const tinygltf::Model& model, int textureIndex) {
    PROFILE_FUNCTION();
    if (textureIndex < 0 || textureIndex >= model.textures.size()) return 0;

    const tinygltf::Texture& texture = model.textures[textureIndex];
    const tinygltf::Image& image = model.images[texture.source];

    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    GLenum internalFormat;
    GLenum format;
    GLenum dataType;

    if (image.bits == 16) {
        dataType = GL_UNSIGNED_SHORT;
        if (image.component == 1) {
            format = GL_RED;
            internalFormat = GL_R16;
        } else if (image.component == 3) {
            format = GL_RGB;
            internalFormat = GL_RGB16;
        } else if (image.component == 4) {
            format = GL_RGBA;
            internalFormat = GL_RGBA16;
        } else {
            format = GL_RGBA;
            internalFormat = GL_RGBA16;
        }
    } else {
        dataType = GL_UNSIGNED_BYTE;
        if (image.component == 1) {
            format = GL_RED;
            internalFormat = GL_R8;
        } else if (image.component == 3) {
            format = GL_RGB;
            internalFormat = GL_SRGB8;
        } else if (image.component == 4) {
            format = GL_RGBA;
            internalFormat = GL_SRGB8_ALPHA8;
        } else {
            format = GL_RGBA;
            internalFormat = GL_SRGB8_ALPHA8;
        }
    }

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, image.width, image.height, 0, format, dataType, image.image.data());
    glGenerateMipmap(GL_TEXTURE_2D);

    if (texture.sampler >= 0 && texture.sampler < model.samplers.size()) {
        const tinygltf::Sampler& sampler = model.samplers[texture.sampler];
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sampler.minFilter >= 0 ? sampler.minFilter : GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sampler.magFilter >= 0 ? sampler.magFilter : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, sampler.wrapS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, sampler.wrapT);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    return textureID;
}

void GLTFMesh::Render(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos) {
    if (!m_loaded || !m_shader) return;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    glm::mat4 model = GetTransform();

    m_shader->Bind();
    m_shader->SetMat4("uModel", model);
    m_shader->SetMat4("uView", view);
    m_shader->SetMat4("uProjection", projection);
    m_shader->SetVec3("uCameraPos", cameraPos);
    m_shader->SetVec3("uLightDir", glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)));

    if (m_useSharedBuffers) {
        glBindVertexArray(m_sharedVAO);
    }

    for (const auto& prim : m_primitives) {
        bool hasTransparency = false;

        if (prim.m_materialIndex >= 0 && prim.m_materialIndex < m_materials.size()) {
            const GLTFMaterial& mat = m_materials[prim.m_materialIndex];
            hasTransparency = mat.m_hasTransparency;

            m_shader->SetVec4("uBaseColorFactor", mat.m_baseColorFactor);
            m_shader->SetFloat("uMetallicFactor", mat.m_metallicFactor);
            m_shader->SetFloat("uRoughnessFactor", mat.m_roughnessFactor);

            if (mat.m_hasBaseColorTexture) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, mat.m_baseColorTexture);
                m_shader->SetInt("uBaseColorTexture", 0);
                m_shader->SetInt("uHasBaseColorTexture", 1);
            } else {
                m_shader->SetInt("uHasBaseColorTexture", 0);
            }
        } else {
            m_shader->SetVec4("uBaseColorFactor", glm::vec4(0.8f));
            m_shader->SetFloat("uMetallicFactor", 0.0f);
            m_shader->SetFloat("uRoughnessFactor", 0.5f);
            m_shader->SetInt("uHasBaseColorTexture", 0);
        }

        if (hasTransparency) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        } else {
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }

        if (m_useSharedBuffers) {
            const void* offsetPtr = reinterpret_cast<const void*>(static_cast<uintptr_t>(prim.m_indexOffsetBytes));
            glDrawElementsBaseVertex(GL_TRIANGLES, prim.m_indexCount, prim.m_indexType, offsetPtr, prim.m_baseVertex);
        } else {
            glBindVertexArray(prim.m_VAO);
            glDrawElements(GL_TRIANGLES, prim.m_indexCount, prim.m_indexType, 0);
            glBindVertexArray(0);
        }
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    if (m_useSharedBuffers) {
        glBindVertexArray(0);
    }
    m_shader->Unbind();
}

glm::mat4 GLTFMesh::GetTransform() const {
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_position);
    transform *= glm::mat4_cast(m_rotation);
    transform = glm::scale(transform, m_scale);
    return transform;
}

GLTFMesh::PhysicsGeometry GLTFMesh::GetPhysicsGeometry() const {
    PhysicsGeometry result;

    if (!m_loaded) return result;

    if (m_useSharedBuffers && m_sharedVBO && m_sharedEBO) {
        glBindBuffer(GL_ARRAY_BUFFER, m_sharedVBO);
        GLint vboSize = 0;
        glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vboSize);

        size_t vertexStride = 8 * sizeof(float);
        size_t vertexCount = vboSize / vertexStride;

        std::vector<float> vertexData(vboSize / sizeof(float));
        glGetBufferSubData(GL_ARRAY_BUFFER, 0, vboSize, vertexData.data());

        for (size_t i = 0; i < vertexCount; ++i) {
            size_t baseIndex = i * 8;
            result.vertices.emplace_back(
                vertexData[baseIndex + 0],
                vertexData[baseIndex + 1],
                vertexData[baseIndex + 2]
            );
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_sharedEBO);
        GLint eboSize = 0;
        glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &eboSize);

        if (!m_primitives.empty()) {
            unsigned int indexType = m_primitives[0].m_indexType;

            if (indexType == GL_UNSIGNED_INT) {
                std::vector<unsigned int> indices(eboSize / sizeof(unsigned int));
                glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, eboSize, indices.data());
                result.indices.assign(indices.begin(), indices.end());
            } else if (indexType == GL_UNSIGNED_SHORT) {
                std::vector<unsigned short> indices(eboSize / sizeof(unsigned short));
                glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, eboSize, indices.data());
                for (auto idx : indices) {
                    result.indices.push_back(static_cast<unsigned int>(idx));
                }
            } else if (indexType == GL_UNSIGNED_BYTE) {
                std::vector<unsigned char> indices(eboSize / sizeof(unsigned char));
                glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, eboSize, indices.data());
                for (auto idx : indices) {
                    result.indices.push_back(static_cast<unsigned int>(idx));
                }
            }
        }
    } else {
        for (const auto& primitive : m_primitives) {
            glBindBuffer(GL_ARRAY_BUFFER, primitive.m_VBO);
            GLint vboSize = 0;
            glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vboSize);

            size_t vertexStride = 8 * sizeof(float);
            size_t vertexCount = vboSize / vertexStride;

            std::vector<float> vertexData(vboSize / sizeof(float));
            glGetBufferSubData(GL_ARRAY_BUFFER, 0, vboSize, vertexData.data());

            size_t indexOffset = result.vertices.size();

            for (size_t i = 0; i < vertexCount; ++i) {
                size_t baseIndex = i * 8;
                result.vertices.emplace_back(
                    vertexData[baseIndex + 0],
                    vertexData[baseIndex + 1],
                    vertexData[baseIndex + 2]
                );
            }

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, primitive.m_EBO);
            GLint eboSize = 0;
            glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &eboSize);

            if (primitive.m_indexType == GL_UNSIGNED_INT) {
                std::vector<unsigned int> indices(eboSize / sizeof(unsigned int));
                glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, eboSize, indices.data());
                for (auto idx : indices) {
                    result.indices.push_back(static_cast<unsigned int>(indexOffset + idx));
                }
            } else if (primitive.m_indexType == GL_UNSIGNED_SHORT) {
                std::vector<unsigned short> indices(eboSize / sizeof(unsigned short));
                glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, eboSize, indices.data());
                for (auto idx : indices) {
                    result.indices.push_back(static_cast<unsigned int>(indexOffset + idx));
                }
            } else if (primitive.m_indexType == GL_UNSIGNED_BYTE) {
                std::vector<unsigned char> indices(eboSize / sizeof(unsigned char));
                glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, eboSize, indices.data());
                for (auto idx : indices) {
                    result.indices.push_back(static_cast<unsigned int>(indexOffset + idx));
                }
            }
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    return result;
}

