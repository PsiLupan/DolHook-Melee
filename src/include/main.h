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

private:
	const hl::IHook *m_serialupdate = nullptr;
	const hl::IHook *m_serialshutdown = nullptr;

    hl::Hooker m_hooker;
};