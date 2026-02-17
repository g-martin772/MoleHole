#include "ntpch.h"
#include "Shader.h"

#include "glad/glad.h"
#include <glm/gtc/type_ptr.hpp>
#include <Nut/Renderer/Renderer.h>
#include <Nut/Platform/OpenGL/OpenGLShader.h>


namespace MHR {
	Ref<Shader> Shader::Create(const std::string& path)
	{
		switch (Renderer::GetAPI())
		{
		case RendererAPI::API::OpenGL:
		{
			return std::make_shared<OpenGLShader>(path);
		}
		default:
			NT_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			break;
		}
		NT_CORE_ASSERT(false, "No API Selected!");
		return nullptr;
	}

	Ref<Shader> Shader::Create(const std::string& name, const std::string& vertexSrc, const std::string FragmentSrc)
	{
		switch(Renderer::GetAPI())
		{
		case RendererAPI::API::OpenGL:
		{
			return std::make_shared<OpenGLShader>(name, vertexSrc, FragmentSrc);
		}
		default:
			NT_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
			break;
		}
		NT_CORE_ASSERT(false, "No API Selected!");
		return nullptr;
	}

	void ShaderLibrary::Add(const Ref<Shader>& s)
	{
		auto& name = s->GetName();
		NT_CORE_ASSERT(m_Shaders.find(name) == m_Shaders.end(), "Shader already exists!");
		m_Shaders[name] = s;
	}

	void ShaderLibrary::Add(const std::string& name, const Ref<Shader>& s)
	{
		NT_CORE_ASSERT(m_Shaders.find(name) == m_Shaders.end(), "Shader already exists!");
		m_Shaders[name] = s;
	}

	Ref<Shader> ShaderLibrary::Load(const std::string& path)
	{
		auto shader = Shader::Create(path);
		Add(shader);
		return shader;
	}

	Ref<Shader> ShaderLibrary::Load(const std::string& name, const std::string& path)
	{
		auto shader = Shader::Create(path);
		Add(name, shader);
		return shader;
	}

	Ref<Shader> ShaderLibrary::Get(const std::string& name)
	{
		NT_CORE_ASSERT(m_Shaders.find(name) != m_Shaders.end(), "Shader not found!");
		return m_Shaders[name];
	}
}
