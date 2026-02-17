#include "ntpch.h"
#include "VertexArray.h"

#include "Renderer.h"
#include "Nut/Platform/OpenGL/OpenGLVertexArray.h"

namespace MHR {
	Ref<VertexArray> VertexArray::Create()
	{
		switch (Renderer::GetAPI()) {
		case RendererAPI::API::OpenGL:	return  std::make_shared<OpenGLVertexArray>();
		}
		NT_CORE_ASSERT(false, "RendererAPI::selected is currently not supported!")
			return nullptr;
	}
}
