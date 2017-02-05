#include "MediaLayer.hpp"

MediaLayer::MediaLayer(void) :
	m_Settings{ Settings::GetInstance() },
	m_IsInit{ false }
{}

void MediaLayer::Init(void)
{
	if (m_IsInit) {
		throw Exception("Called MediaLayer::Init twice");
	}

	// TODO: Implement

	m_IsInit = true;
	return;
}

void MediaLayer::Quit(void)
{
	if (!m_IsInit) {
		throw Exception("Called MediaLayer::Quit before Display::Init");
	}

	// TODO: Implement

	m_IsInit = false;
	return;
}

void MediaLayer::SwapBuffers(void)
{
	if (!m_IsInit) {
		throw Exception("Called MediaLayer::SwapBuffers before Display::Init");
	}

	// TODO: Implement
}

void MediaLayer::ShowDialog(const std::string& title, const std::string& text)
{
	// To avoid warnings until implementation
	(void) title;
	(void) text;

	if (!m_IsInit) {
		throw Exception("Called MediaLayer::ShowDialog before Display::Init");
	}

	// TODO: Implement
}
