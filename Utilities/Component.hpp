#pragma once

struct Component
{
    virtual void Init(void) = 0;
    virtual void Quit(void) = 0;

protected:
    bool m_IsInit;
};