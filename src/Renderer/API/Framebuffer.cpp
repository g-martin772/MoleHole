#include "ntpch.h"
#include "Framebuffer.h"

#include "Nut/Platform/OpenGL/OpenGLFramebuffer.h"
#include "Nut/Renderer/Renderer.h"

namespace MHR {
    Ref<FrameBuffer> FrameBuffer::Create(const FrameBufferSpecs& specs)
    {
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::OpenGL:
		{
			return std::make_shared<OpenGLFrameBuffer>(specs);
		}
		default:
			NT_ERROR("No API Selected!");
			NT_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			break;
		}
		return nullptr;
    }
}
