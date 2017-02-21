#pragma once

// Utilities
#include <Utilities/Exception.hpp>
#include <Utilities/FileToString.hpp>

#include <RenderingEngine/Material.hpp>
#include <RenderingEngine/Renderers/Renderer.hpp>

class StandardRenderer :
	public Renderer
{
	StandardRenderer(void) :
		Renderer()
	{}

public:
	static StandardRenderer* GetInstance(void)
	{
		static StandardRenderer* instance = nullptr;
		if(instance == nullptr) {
			instance = new StandardRenderer();
		}
		return instance;
	}

	void Init(void) override;
	void Quit(void) override;

	void SetupCamera(void) override;
	void SetupMaterial(const Material& material) override;
};
