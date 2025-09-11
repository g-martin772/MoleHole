#pragma once
#include <glad/gl.h>

class VertexBuffer {
public:
    VertexBuffer(const void* data, unsigned int size, GLenum usage = GL_STATIC_DRAW);
    ~VertexBuffer();
    void Bind() const;
    void Unbind() const;
    unsigned int GetID() const { return m_ID; }
private:
    unsigned int m_ID;
};

class IndexBuffer {
public:
    IndexBuffer(const unsigned int* data, unsigned int count, GLenum usage = GL_STATIC_DRAW);
    ~IndexBuffer();
    void Bind() const;
    void Unbind() const;
    unsigned int GetID() const { return m_ID; }
    unsigned int GetCount() const { return m_Count; }
private:
    unsigned int m_ID;
    unsigned int m_Count;
};

class VertexArray {
public:
    VertexArray();
    ~VertexArray();
    void Bind() const;
    void Unbind() const;
    unsigned int GetID() const { return m_ID; }
    void EnableAttrib(unsigned int index, int size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
private:
    unsigned int m_ID;
};

