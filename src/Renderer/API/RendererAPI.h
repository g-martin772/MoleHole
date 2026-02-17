#pragma once

#include "Nut/Renderer/Buffer.h"
#include "Nut/Renderer/VertexArray.h"

#include <glm/glm.hpp>
#include <memory>

namespace MHR{
	class RendererAPI {
	public:
		enum class API
		{
			None = 0,
			OpenGL = 1,
			Vulcan = 2,
			Medal = 3,
			DirectX9 = 9,
			DirextX10 = 10,
			DirectX11 = 11,
			DirectX12 = 12
		};

		virtual void Init() = 0;
		virtual void SetViewport(uint32_t originX, uint32_t originY, uint32_t width, uint32_t height) = 0;
		virtual void SetClearColor(const glm::vec4& color) = 0;
		virtual void Clear() = 0;

		virtual void DrawIndexed(const Ref<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;
		virtual void DrawLines(const Ref<VertexArray>& vertexArray, uint32_t vertexCount) = 0;

		virtual void SetLineWidth(float lineWidth) = 0;

		inline static API GetApi() { return s_API; }
	private:
		static API s_API;
	};
}
