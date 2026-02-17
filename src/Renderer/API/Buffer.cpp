#include "ntpch.h"
#include "Buffer.h"

#include "Renderer/Renderer.h"
#include "Platform/OpenGL/OpenGLBuffer.h"

namespace MHR{
	Ref<VertexBuffer> VertexBuffer::Create(uint32_t count)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::OpenGL:
		{
			return std::make_shared<OpenGLVertexBuffer>(count * sizeof(uint32_t));
		}
		default:
			NT_WARN("No API Selected!");
			NT_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			break;
		}
		return nullptr;
	}

	Ref<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t count)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::OpenGL:
		{
			return std::make_shared<OpenGLVertexBuffer>(vertices, count * sizeof(float));
		}
		default:
			NT_WARN("No API Selected!");
			NT_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			break;
		}
		return nullptr;
	}

	Ref<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t count)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::OpenGL:
		{
			return std::make_shared<OpenGLIndexBuffer>(indices, count * sizeof(uint32_t));
		}
		default:
			NT_WARN("No API Selected!");
			NT_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			break;
		}
		return nullptr;
	}
}
