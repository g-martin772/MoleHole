#pragma once

#include "Nut/Renderer/RenderCommand.h"
#include <Nut/Camera/OrthographicCamera.h>
#include <Nut/Renderer/Shader.h>
#include <Nut/Events/AppEvent.h>

namespace MHR {
	class Renderer {
	public:
		Renderer();
		~Renderer();

		static void Init();
		static void OnWindowResize(WindowResizeEvent& e);

		static void BeginScene(OrthographicCamera& camera);
		static void EndScene();

		static void Submit(const Ref<Shader>& shader, const Ref<VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f));

		inline static RendererAPI::API GetAPI() { return RendererAPI::GetApi(); }
	private:
		struct SceneData {
			glm::mat4 ViewProjectionMatrix;
		};
		static SceneData* m_SceneData;
	};

}
