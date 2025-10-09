#include "GLTFMesh.h"
#include "Shader.h"
#include <glad/gl.h>
#include <tiny_gltf.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/type_ptr.hpp>

GLTFPrimitive::GLTFPrimitive()
    : m_VAO(0), m_VBO(0), m_EBO(0), m_indexCount(0), m_materialIndex(-1) {}

GLTFPrimitive::~GLTFPrimitive() {
    Cleanup();
}

void GLTFPrimitive::Cleanup() {
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO) glDeleteBuffers(1, &m_VBO);
    if (m_EBO) glDeleteBuffers(1, &m_EBO);
    m_VAO = m_VBO = m_EBO = 0;
}

GLTFMaterial::GLTFMaterial()
    : m_baseColorFactor(1.0f), m_metallicFactor(1.0f), m_roughnessFactor(1.0f),
      m_baseColorTexture(0), m_hasBaseColorTexture(false) {}

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
    m_loaded = false;
}

bool GLTFMesh::Load(const std::string& path) {
    Cleanup();

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

    const tinygltf::Scene& scene = model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];
    for (int nodeIdx : scene.nodes) {
        ProcessNode(model, model.nodes[nodeIdx], glm::mat4(1.0f));
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

        const float* positions = reinterpret_cast<const float*>(
            &posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset]);

        const float* normals = nullptr;
        if (normalAccessorIdx >= 0) {
            const tinygltf::Accessor& normAccessor = model.accessors[normalAccessorIdx];
            const tinygltf::BufferView& normBufferView = model.bufferViews[normAccessor.bufferView];
            const tinygltf::Buffer& normBuffer = model.buffers[normBufferView.buffer];
            normals = reinterpret_cast<const float*>(
                &normBuffer.data[normBufferView.byteOffset + normAccessor.byteOffset]);
        }

        const float* texcoords = nullptr;
        if (texcoordAccessorIdx >= 0) {
            const tinygltf::Accessor& texAccessor = model.accessors[texcoordAccessorIdx];
            const tinygltf::BufferView& texBufferView = model.bufferViews[texAccessor.bufferView];
            const tinygltf::Buffer& texBuffer = model.buffers[texBufferView.buffer];
            texcoords = reinterpret_cast<const float*>(
                &texBuffer.data[texBufferView.byteOffset + texAccessor.byteOffset]);
        }

        for (size_t i = 0; i < posAccessor.count; ++i) {
            vertexData.push_back(positions[i * 3 + 0]);
            vertexData.push_back(positions[i * 3 + 1]);
            vertexData.push_back(positions[i * 3 + 2]);

            if (normals) {
                vertexData.push_back(normals[i * 3 + 0]);
                vertexData.push_back(normals[i * 3 + 1]);
                vertexData.push_back(normals[i * 3 + 2]);
            } else {
                vertexData.push_back(0.0f);
                vertexData.push_back(1.0f);
                vertexData.push_back(0.0f);
            }

            if (texcoords) {
                vertexData.push_back(texcoords[i * 2 + 0]);
                vertexData.push_back(texcoords[i * 2 + 1]);
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
        if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            std::vector<unsigned int> indices32;
            const unsigned short* indices16 = reinterpret_cast<const unsigned short*>(indexData);
            for (size_t i = 0; i < indexAccessor.count; ++i) {
                indices32.push_back(static_cast<unsigned int>(indices16[i]));
            }
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices32.size() * sizeof(unsigned int), indices32.data(), GL_STATIC_DRAW);
        } else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexAccessor.count * sizeof(unsigned int), indexData, GL_STATIC_DRAW);
        } else {
            std::vector<unsigned int> indices32;
            const unsigned char* indices8 = indexData;
            for (size_t i = 0; i < indexAccessor.count; ++i) {
                indices32.push_back(static_cast<unsigned int>(indices8[i]));
            }
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices32.size() * sizeof(unsigned int), indices32.data(), GL_STATIC_DRAW);
        }

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

        glBindVertexArray(0);

        m_primitives.push_back(std::move(prim));
    }
}

void GLTFMesh::LoadMaterials(const tinygltf::Model& model) {
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

        if (mat.pbrMetallicRoughness.baseColorTexture.index >= 0) {
            material.m_baseColorTexture = LoadTextureFromModel(model, mat.pbrMetallicRoughness.baseColorTexture.index);
            material.m_hasBaseColorTexture = true;
        }

        m_materials.push_back(material);
    }
}

unsigned int GLTFMesh::LoadTextureFromModel(const tinygltf::Model& model, int textureIndex) {
    if (textureIndex < 0 || textureIndex >= model.textures.size()) return 0;

    const tinygltf::Texture& texture = model.textures[textureIndex];
    const tinygltf::Image& image = model.images[texture.source];

    unsigned int textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    GLenum format = GL_RGBA;
    if (image.component == 1) format = GL_RED;
    else if (image.component == 3) format = GL_RGB;
    else if (image.component == 4) format = GL_RGBA;

    glTexImage2D(GL_TEXTURE_2D, 0, format, image.width, image.height, 0, format, GL_UNSIGNED_BYTE, image.image.data());
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

    glm::mat4 model = GetTransform();

    m_shader->Bind();
    m_shader->SetMat4("uModel", model);
    m_shader->SetMat4("uView", view);
    m_shader->SetMat4("uProjection", projection);
    m_shader->SetVec3("uCameraPos", cameraPos);
    m_shader->SetVec3("uLightDir", glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f)));

    for (const auto& prim : m_primitives) {
        if (prim.m_materialIndex >= 0 && prim.m_materialIndex < m_materials.size()) {
            const GLTFMaterial& mat = m_materials[prim.m_materialIndex];
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

        glBindVertexArray(prim.m_VAO);
        glDrawElements(GL_TRIANGLES, prim.m_indexCount, GL_UNSIGNED_INT, 0);
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

