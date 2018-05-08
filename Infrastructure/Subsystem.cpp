#include <Infrastructure/Subsystem.hpp>

Infrastructure::Subsystem::Subsystem() :
    m_IsInitialized { false }
{}

const bool Infrastructure::Subsystem::IsInitialized() const
{
    return m_IsInitialized;
}
