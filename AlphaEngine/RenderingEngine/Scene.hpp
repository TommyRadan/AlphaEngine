#pragma once

#include <Utilities\Singleton.hpp>

#include "Model.hpp"

#include <list>

class Scene : public Singleton<Scene>
{
	friend Singleton<Scene>;
	Scene() {}

public:
	void AddModel(Model* const model);
	void Render(void);

private:
	std::list<Model*> m_Models;
};
