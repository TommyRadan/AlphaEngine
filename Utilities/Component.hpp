#pragma once

/*
 * This class provides interface every module
 * needs to follow.
 *
 * All modules are supposed to implement this
 * and Singleton
 */

struct Component
{
    virtual void Init(void) = 0;
    virtual void Quit(void) = 0;

protected:
    bool m_IsInit;
};
