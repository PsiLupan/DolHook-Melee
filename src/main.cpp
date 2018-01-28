#include "main.h"

hl::StaticInit<class SmashMain> g_main;

//void hkMemlo(hl::CpuContext *ctx);

//16 _byteswap_ushort(unsigned short value);
//32 _byteswap_ulong(unsigned long value);
//64 _byteswap_uint64(unsigned __int64 value);

uintptr_t cache_invalidate = NULL;
uintptr_t mem_getptr = NULL;
uintptr_t memlo = NULL; //80000000
uintptr_t entity = NULL;

bool SmashMain::init() {
		m_con.create("SmashData");
		m_con.printf("SmashData initiated\n");

		mem_getptr = hl::FindPattern("48 83 EC 38 81 E1 FF FF FF 3F 81 F9 00 00 80 01 73 0E 8B C1");

		if (!mem_getptr) {
			m_con.printf("[Core::Init] Memory::GetPointer pattern invalid\n");
			return false;
		}
		m_con.printf("[Core::Init] Memory_GetPointer(): %p\n", mem_getptr);
		memlo = ((uintptr_t(__thiscall*)(uintptr_t, unsigned int))mem_getptr)(NULL, 0x80000000);
		if (memlo == 0) {
			m_con.printf("[WARNING]: Game not started\n");
			m_con.printf("Press Insert once game is started to grab pointers\n");
		}
		else {
			updatePtrs();
		}

		cache_invalidate = hl::FindPattern("40 56 48 83 EC 20 F7 05 D0 C1 D4 00 00 80 00 00 8B F2 0F 84 67 03 00 00 48 89 5C 24 30");
		if (!cache_invalidate) {
			m_con.printf("[Core::Init] PPCCache::Invalidate pattern invalid\n");
		}
		m_con.printf("[Core::Init] PPCCache::Invalidate(): %p\n", cache_invalidate);

		//m_hkMemlo = m_hooker.hookDetour(mem_getptr + 0x1F, 0x0E, hkMemlo);

		/*if (!m_hkMemlo) {
			m_con.printf("[hkMemLo] Failed to hook.\n");
			cleanup();
		}*/
		return true;
}

bool SmashMain::step() {
	/*if (GetAsyncKeyState(VK_INSERT) < 0) {
	for (int i = 0; i < 0xD4; i++) {
	char* addr = reinterpret_cast<char*>(nametagRegion + i);
	*addr = 'D';
	}
	//Write the original stack trace
	uint32_t* addr = reinterpret_cast<uint32_t*>(nametagRegion + 212);
	*addr = _byteswap_ulong(0x804EE8F8);

	//Write the address of our injected code
	addr = reinterpret_cast<uint32_t*>(nametagRegion + 216);
	*addr = _byteswap_ulong(0x8045D930);

	m_con.printf("[Patcher] Wrote Nametag Overflow");
	Sleep(500);
	}*/
	if (GetAsyncKeyState(VK_END) < 0) {
		cleanup();
		return false;
	}
	if (GetAsyncKeyState(VK_INSERT) < 0) {
		updatePtrs();
		Sleep(500);
	}
	return true;
}

void SmashMain::cleanup() {
	/*if(m_hkMemlo) 
		m_hooker.unhook(m_hkMemlo);*/
}

void SmashMain::updatePtrs() {
	memlo = ((uintptr_t(__thiscall*)(uintptr_t, unsigned int))mem_getptr)(NULL, 0x80000000);
	m_con.printf("[Core::Ptrs] memlo: %p\n", memlo);
	
	//Memory is going to be contiguous with memlo, but should probably just make a habit of checking w/ the function
	nametagRegion = memlo + 0x045D850; //Start of the nametag list for writing
	m_con.printf("[Core::Ptrs] Nametag Regions: %p\n", nametagRegion);
	
	patchStart = memlo + 0x045D930; //8045D930, this is the free region after nametags for say, a buffer overflow
	m_con.printf("[Core::Ptrs] Patch Start: %p\n", patchStart);
	
	firstEntity = memlo + 0x0BDA4A0; //80BDA4A0, this is a location the game stores a ptr to the first loaded entity
	m_con.printf("[Core::Ptrs] Loaded Entity Ptr: %p\n", firstEntity);
	uint32_t* addr = reinterpret_cast<uint32_t*>(firstEntity);
	if (*addr == 0) {
		m_con.printf("[WARNING] No loaded entities currently\n");
	}
	else {
		entity = memlo + (_byteswap_ulong(*addr) - 0x80000000);
		m_con.printf("[Core::Ptrs] First Entity: %p\n", entity);
	}
}

/*void hkMemlo(hl::CpuContext *ctx) {
	memlo = (uintptr_t)ctx->RAX;
}*/