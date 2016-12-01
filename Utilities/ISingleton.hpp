#pragma once

template <typename Type>
class ISingleton
{
protected:
	ISingleton(void) = default;
	virtual ~ISingleton(void) = default;

public:
	static Type* GetInstance(void) {
		static Type* instance = nullptr;
		if (instance == nullptr) {
			instance = new Type();
		}
		return instance;
	}
};
