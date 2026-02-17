#include "ntpch.h"
#include "UniformBuffer.h"

#include "Nut/Renderer/Renderer.h"
#include "Nut/Platform/OpenGL/OpenGLUniformBuffer.h"

namespace MHR {
	Ref<UniformBuffer> MHR::UniformBuffer::Create(uint32_t size, uint32_t binding)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::None:    NT_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
		case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLUniformBuffer>(size, binding);
		}

		NT_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}
}