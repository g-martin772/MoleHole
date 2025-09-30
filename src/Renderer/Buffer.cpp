#include <glad/gl.h>
#include "Buffer.h"

static GLenum ToGL(BufferUsage usage) {
    switch (usage) {
        case BufferUsage::StaticDraw: return GL_STATIC_DRAW;
        case BufferUsage::DynamicDraw: return GL_DYNAMIC_DRAW;
        default: return GL_STATIC_DRAW;
    }
}

VertexBuffer::VertexBuffer(const void *data, unsigned int size, BufferUsage usage)
    : m_ID(0) {
    glGenBuffers(1, &m_ID);
    glBindBuffer(GL_ARRAY_BUFFER, m_ID);
    glBufferData(GL_ARRAY_BUFFER, size, data, ToGL(usage));
}

VertexBuffer::~VertexBuffer() {
    glDeleteBuffers(1, &m_ID);
}

void VertexBuffer::Bind() const {
    glBindBuffer(GL_ARRAY_BUFFER, m_ID);
}

void VertexBuffer::Unbind() const {
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

IndexBuffer::IndexBuffer(const unsigned int *data, unsigned int count, BufferUsage usage)
    : m_Count(count), m_ID(0) {
    glGenBuffers(1, &m_ID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(unsigned int), data, ToGL(usage));
}

IndexBuffer::~IndexBuffer() {
    glDeleteBuffers(1, &m_ID);
}

void IndexBuffer::Bind() const {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ID);
}

void IndexBuffer::Unbind() const {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

VertexArray::VertexArray()
    : m_ID(0) {
    glGenVertexArrays(1, &m_ID);
}

VertexArray::~VertexArray() {
    glDeleteVertexArrays(1, &m_ID);
}

void VertexArray::Bind() const {
    glBindVertexArray(m_ID);
}

void VertexArray::Unbind() const {
    glBindVertexArray(0);
}

void VertexArray::EnableAttrib(unsigned int index, int size, int type, bool normalized, int stride,
                               const void *pointer) {
    glEnableVertexAttribArray(index);
    glVertexAttribPointer(index, size, type, normalized ? GL_TRUE : GL_FALSE, stride, pointer);
}
