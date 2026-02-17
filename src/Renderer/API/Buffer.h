#pragma once

namespace MHR {
	enum class ShaderDataType {
		None = 0,
		Float, Float2, Float3, Float4,
		Mat3, Mat4,
		Int, Int2, Int3, Int4,
		Struct, Bool
	};

	static std::uint32_t ShaderDataTypeSize(ShaderDataType type) {
		switch (type) {
			 case ShaderDataType::Float:		return 4 * 1;
			 case ShaderDataType::Float2:		return 4 * 2;
			 case ShaderDataType::Float3:		return 4 * 3;
			 case ShaderDataType::Float4:		return 4 * 4;
			 case ShaderDataType::Mat3:			return 4 * 3 * 3;
			 case ShaderDataType::Mat4:			return 4 * 4 * 4;
			 case ShaderDataType::Int:			return 4 * 1;
			 case ShaderDataType::Int2:			return 4 * 2;
			 case ShaderDataType::Int3:			return 4 * 3;
			 case ShaderDataType::Int4:			return 4 * 4;
			 case ShaderDataType::Bool:			return 1;
			 //case ShaderDataType::Struct:			return 4;
			 default:
				 NT_CORE_ASSERT(false, "Unknown ShaderDataType!");
				 return 0;
		}
		return 0;
	}

	struct BufferElement {
		std::string Name;
		ShaderDataType Type;
		uint32_t Offset;
		uint32_t Size;
		bool Normalized;

		BufferElement() {}
		BufferElement(ShaderDataType type, const std::string& name, bool normalized = false)
			: Name(name), Type(type), Size(ShaderDataTypeSize(type)), Offset(0), Normalized(normalized) {}

		uint32_t GetComponentCount() const {
			switch (Type)
			{
				case ShaderDataType::Float:			return 1;
				case ShaderDataType::Float2:		return 2;
				case ShaderDataType::Float3:		return 3;
				case ShaderDataType::Float4:		return 4;
				case ShaderDataType::Mat3:			return 3 * 3;
				case ShaderDataType::Mat4:			return 4 * 4;
				case ShaderDataType::Int:			return 1;
				case ShaderDataType::Int2:			return 2;
				case ShaderDataType::Int3:			return 3;
				case ShaderDataType::Int4:			return 4;
				case ShaderDataType::Bool:			return 1;
				default:
					NT_CORE_ASSERT(false, "Unknown ShaderDataType!");
					return 0;
			}
			return 0;
		}
	};

	class BufferLayout {
	public:
		BufferLayout() {}
		BufferLayout(const std::initializer_list<BufferElement>& elements) : m_Elemnts(elements) {
			CalculateOffsetAndStride();
		}

		inline uint32_t GetStride() const { return m_Stride; }
		inline const std::vector<BufferElement>& GetElements() const { return m_Elemnts; }

		std::vector<BufferElement>::iterator begin() { return m_Elemnts.begin(); }
		std::vector<BufferElement>::iterator end() { return m_Elemnts.end(); }
		std::vector<BufferElement>::const_iterator begin() const { return m_Elemnts.begin(); }
		std::vector<BufferElement>::const_iterator end() const{ return m_Elemnts.end(); }
	private:
		void CalculateOffsetAndStride() {
			uint32_t ofset = 0;
			m_Stride = 0;
			for (auto& element : m_Elemnts) {
				element.Offset = ofset;
				ofset += element.Size;
				m_Stride += element.Size;
			}
		}
		std::vector<BufferElement> m_Elemnts;
		uint32_t m_Stride = 0;
	};

	class VertexBuffer {
	public:
		virtual ~VertexBuffer() {}

		virtual void Bind() const = 0;
		virtual void UnBind() const= 0;

		virtual void SetLayout(const BufferLayout& layout) = 0;
		virtual const BufferLayout& GetLayout() const = 0;

		virtual void SetData(const void* data, uint32_t size) = 0;

		static Ref<VertexBuffer> Create(uint32_t size);
		static Ref<VertexBuffer> Create(float* vertices, uint32_t size);
	};


	// TODO 16bit index buffer
	class IndexBuffer {
	public:
		virtual ~IndexBuffer() {}

		virtual void Bind() const = 0;
		virtual void UnBind() const = 0;

		virtual uint32_t GetCount() const = 0;

		static Ref<IndexBuffer> Create(uint32_t* indices, uint32_t size);
	};
}
