#pragma once

#include "Nut/Renderer/RendererAPI.h"

namespace MHR {
	class RenderCommand {
	public:
		inline static void Init() {
			NT_PROFILE_FUNCTION();
			s_RendererAPI->Init();
		}

		inline static void SetViewport(uint32_t originX, uint32_t originY, uint32_t width, uint32_t height) {
			s_RendererAPI->SetViewport(originX, originY, width, height);
		}

		inline static void SetClearColor(const glm::vec4& color) {
			s_RendererAPI->SetClearColor(color);
		}

		inline static void Clear() {
			s_RendererAPI->Clear();
		}

		inline static void DrawIndexed(const Ref<VertexArray> vertexArray, uint32_t indexCount = 0) {
			NT_PROFILE_FUNCTION();
			s_RendererAPI->DrawIndexed(vertexArray, indexCount);
		}

		inline static void DrawLines(const Ref<VertexArray> vertexArray, uint32_t vertexCount = 0) {
			NT_PROFILE_FUNCTION();
			s_RendererAPI->DrawLines(vertexArray, vertexCount);
		}

		inline static void SetLineWidth(float lineWidth) {
			NT_PROFILE_FUNCTION();
			s_RendererAPI->SetLineWidth(lineWidth);
		}
	private:
		static RendererAPI* s_RendererAPI;
	};
}