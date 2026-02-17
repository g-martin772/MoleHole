#pragma once

#include <string>
#include <glm/glm.hpp>

namespace MHR {
	class Shader {
	public:
		virtual ~Shader() {}

		virtual void Bind() const = 0;
		virtual void UnBind() const = 0;
		
		virtual void SetInt(const std::string& name, int value) = 0;
		virtual void SetIntArr(const std::string& name, int* values, uint32_t count) = 0;
		virtual void SetFloat(const std::string& name, float value) = 0;
		virtual void SetFloat3(const std::string& name, const glm::vec3& value) = 0;
		virtual void SetFloat4(const std::string& name, const glm::vec4& value) = 0;
		virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;

		virtual const std::string& GetName() const = 0;

		static Ref<Shader> Create(const std::string& path);
		static Ref<Shader> Create(const std::string& name, const std::string& vertexSrc, const std::string FragmentSrc);
		//virtual void UploadUniformBuffer();
	private:
	};

	class ShaderLibrary {
	public:
		void Add(const Ref<Shader>& s);
		void Add(const std::string& name, const Ref<Shader>& s);
		Ref<Shader> Load(const std::string& path);
		Ref<Shader> Load(const std::string& name, const std::string& path);

		Ref<Shader> Get(const std::string& name);
	private:
		std::unordered_map<std::string, Ref<Shader>> m_Shaders;
	};
}