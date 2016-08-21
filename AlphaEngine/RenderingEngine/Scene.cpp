#include "Scene.hpp"

void Scene::AddModel(Model* const model)
{
	m_Models.push_back(model);
}

void Scene::Render(void)
{
	for (auto& model : m_Models) {
		model->Render();
	}
}
