#pragma once

template <typename Type>
class Singleton
{
public:
	static Type* GetInstance(void)
	{
		if (m_Instance == nullptr) {
			m_Instance = new Type();
		}
		return m_Instance;
	}

	static void DestroyInstance(void)
	{
		if (m_Instance == nullptr) return;
		delete m_Instance;
	}

protected:
	Singleton(void)
	{
		Singleton::m_Instance = static_cast<Type*>(this);
	}

	virtual ~Singleton(void) = default;

private:
	Singleton(const Singleton&);
	Singleton& operator=(const Singleton&);

	static Type* m_Instance;
};

template<typename Type>
Type* Singleton<Type>::m_Instance = nullptr;
