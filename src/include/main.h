#pragma once
#include "hacklib/ConsoleEx.h"
#include "hacklib/Hooker.h"
#include "hacklib/PatternScanner.h"
#include "hacklib/Main.h"
#include <cstdio>
#include <intrin.h>

class SmashMain : public hl::Main
{
public:
    bool init() override;
    bool step() override;

    const hl::IHook *m_hkMemlo = nullptr;

    uintptr_t nametagRegion = NULL;
    uintptr_t patchStart = NULL;
    uintptr_t firstEntity = NULL;

private:
    void cleanup();
    void updatePtrs();

    hl::ConsoleEx m_con;
    hl::Hooker m_hooker;
};