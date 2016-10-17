#pragma once

template <typename Type>
class Singleton 
{
protected:
	Singleton(void) = default;
	virtual ~Singleton(void) = default;

public:
	static Type* GetInstance(void) {
		static Type* instance = nullptr;
		if (instance == nullptr) {
			instance = new Type();
		}
		return instance;
	}
};
