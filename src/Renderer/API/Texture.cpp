#include "ntpch.h"
#include "Texture.h"

#include <Nut/Renderer/Renderer.h> 
#include <Nut/Platform/OpenGL/OpenGLTexture.h>

namespace MHR {

	Ref<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::OpenGL:
		{
			return std::make_shared<OpenGLTexture2D>(width, height);
		}
		default:
			NT_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			break;
		}
		NT_CORE_ASSERT(false, "No API Selected!");
		return nullptr;
	}

	Ref<Texture2D> Texture2D::Create(const std::string& path)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::OpenGL:
		{
			return std::make_shared<OpenGLTexture2D>(path);
		}
		default:
			NT_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			break;
		}
		NT_CORE_ASSERT(false, "No API Selected!");
		return nullptr;
	}

	SubTexture2D::SubTexture2D(const Ref<Texture2D>& texture, const glm::vec2& min, const glm::vec2& max) : m_Texture(texture)
	{
		m_TexCoords[0] = { min.x, min.y };
		m_TexCoords[1] = { max.x, min.y };
		m_TexCoords[2] = { max.x, max.y };
		m_TexCoords[3] = { min.x, max.y };
	}

	Ref<SubTexture2D> SubTexture2D::CreateFromCoords(const Ref<Texture2D>& texture, const glm::vec2& coords, const glm::vec2& size)
	{
		glm::vec2 min = { (coords.x * size.x) / texture->GetWidth(), (coords.y * size.y) / texture->GetHeight() };
		glm::vec2 max = { ((coords.x + 1) * size.x) / texture->GetWidth(), ((coords.y + 1) * size.y) / texture->GetHeight() };
		return std::make_shared<SubTexture2D>(texture, min, max);
	}

	Ref<SubTexture2D> SubTexture2D::CreateFromCoords(const Ref<Texture2D>& texture, const glm::vec2& coords, const glm::vec2& cellSize, const glm::vec2& cellFactor)
	{
		glm::vec2 min = { (coords.x * cellSize.x) / texture->GetWidth(), (coords.y * cellSize.y) / texture->GetHeight() };
		glm::vec2 max = { ((coords.x + cellFactor.x) * cellSize.x) / texture->GetWidth(), ((coords.y + cellFactor.y) * cellSize.y) / texture->GetHeight() };
		return std::make_shared<SubTexture2D>(texture, min, max);
	}
}

