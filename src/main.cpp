#include "main.h"

hl::StaticInit<class SmashMain> g_main;

hl::ConsoleEx m_con;

void hkSerialUpdate(hl::CpuContext *ctx);

uintptr_t cache_invalidate = NULL;
uintptr_t mem_getptr = NULL;
uintptr_t g_serialupdate = NULL;

uintptr_t memlo = NULL;
uintptr_t nametagRegion = NULL;
uintptr_t patchStart = NULL;
uintptr_t firstEntity = NULL;
uintptr_t timer = NULL;


struct Entity
{
	uintptr_t entity_addr;
	char* player_addr;
	unsigned char player_mem[0x240F];
};
Entity entity[6] = { NULL, NULL, NULL, NULL, NULL, NULL };

//16 _byteswap_ushort(unsigned short value);
//32 _byteswap_ulong(unsigned long value);
//64 _byteswap_uint64(unsigned __int64 value);

uintptr_t getPointer(unsigned int address) {
	return ((uintptr_t(__thiscall*)(uintptr_t, unsigned int))mem_getptr)(NULL, address);
}

void cleanup() {
	memlo = NULL;
	nametagRegion = NULL;
	patchStart = NULL;
	firstEntity = NULL;
	timer = NULL;
	for (int i = 0; i < 6; i++) {
		entity[i].entity_addr = NULL;
		entity[i].player_addr = NULL;
		memset(entity[i].player_mem, 0, 0x240F);
	}
}

void cleanupPlayerPtrs() {
	for (int i = 0; i < 6; i++) {
		entity[i].entity_addr = NULL;
		entity[i].player_addr = NULL;
		memset(entity[i].player_mem, 0, 0x240F);
	}
}

void updatePtrs() {
	memlo = getPointer(0x80000000);

	//Memory is going to be contiguous with memlo, but should probably just make a habit of checking w/ the function
	nametagRegion = memlo + 0x045D850; //Start of the nametag list for writing

	patchStart = memlo + 0x045D930; //This is the free region after nametags for say, a buffer overflow

	firstEntity = memlo + 0x0BDA4A0; //This is a location the game stores a ptr to the first loaded entity

	timer = memlo + 0x046B6C4;
	m_con.printf("[Core::Ptrs] Pointers loaded\n", timer);
}

void updatePlayerPtrs() {
	uint32_t* addr = reinterpret_cast<uint32_t*>(firstEntity);
	if (*addr != 0 && _byteswap_ulong(*addr) > 0x80000000) {
		for (int i = 0; i < 6; i++) {
			if (i > 0)
				addr = reinterpret_cast<uint32_t*>(entity[i - 1].entity_addr + 0x08); //Next entity is entity ptr + 0x0C
			if (*addr == 0 || _byteswap_ulong(*addr) < 0x80000000)
				break;
			entity[i].entity_addr = memlo + (_byteswap_ulong(*addr) - 0x80000000);
			m_con.printf("[INFO] Entity %d MEM1: %p\n", i + 1, _byteswap_ulong(*addr));

			addr = reinterpret_cast<uint32_t*>(entity[i].entity_addr + 0x2C); //Now we'll jump to their player struct
			entity[i].player_addr = reinterpret_cast<char*>(memlo + (_byteswap_ulong(*addr) + 0x10 - 0x80000000)); //Start at 0x10 of the player struct, since everything prior isn't going to change
		}
	}
}

//TODO: Hook unload
void hkSerialUpdate(hl::CpuContext *ctx) {
	if (memlo != NULL) {
		if (timer != NULL) {
			uint32_t* addr = reinterpret_cast<uint32_t*>(timer);
			if (_byteswap_ulong(*addr) > 0) {
				addr = reinterpret_cast<uint32_t*>(firstEntity);
				if (_byteswap_ulong(*addr) > 0x80000000) {
					if (entity[0].entity_addr == NULL) {
						updatePlayerPtrs();
					}
					if (entity[0].entity_addr != NULL) {
						for (int i = 0; i < 6; i++) {
							if (entity[i].entity_addr != NULL) {
								memcpy(&entity[i].player_mem, entity[i].player_addr, 0x240F);
							}
							else {
								break;
							}
						}
					}
				}
				else if (entity[0].entity_addr != NULL) {
					cleanupPlayerPtrs();
				}
			}
		}
	}
	else {
		updatePtrs();
	}
}

bool SmashMain::init() {
		m_con.create("SmashData");

		cache_invalidate = hl::FindPattern("40 56 48 83 EC 20 F7 05 D0 C1 D4 00 00 80 00 00 8B F2 0F 84 67 03 00 00 48 89 5C 24 30");
		if (!cache_invalidate) {
			m_con.printf("[Core::Init] PPCCache::Invalidate pattern invalid\n");
			return false;
		}
		m_con.printf("[Core::Init] PPCCache::Invalidate(): %p\n", cache_invalidate);

		mem_getptr = hl::FindPattern("48 83 EC 38 81 E1 FF FF FF 3F 81 F9 00 00 80 01 73 0E 8B C1");

		if (!mem_getptr) {
			m_con.printf("[Core::Init] Memory::GetPointer pattern invalid\n");
			return false;
		}
		m_con.printf("[Core::Init] Memory_GetPointer(): %p\n", mem_getptr);
		
		g_serialupdate = hl::FindPattern("48 83 EC 28 48 8D 0D 85 FE A4 00 E8 E0 2E EF FF");
		if (!g_serialupdate) {
			m_con.printf("[Core::Init] SerialInterface::UpdateDevices() pattern invalid\n");
			return false;
		}
		m_con.printf("[Core::Init] SerialInterface::UpdateDevices(): %p\n",g_serialupdate);
		m_serialupdate = m_hooker.hookDetour(g_serialupdate + 0x2b, 15, hkSerialUpdate);
		if (m_serialupdate == nullptr) {
			m_con.printf("[Core::Init] SerialInterface::UpdateDevices() hook failed\n");
			return false;
		}
		else {
			m_con.printf("[Core::Init] SerialInterface::UpdateDevices() hook successful\n");
		}

		cleanup();
		
		return true;
}
/* Step occurs every 10ms, so don't rely on this for sending packets on frame.
*/
bool SmashMain::step() {
	if (GetAsyncKeyState(VK_END) < 0) {
		m_hooker.unhook(m_serialupdate);
		m_con.printf("[Core::Cleanup] SerialInterface::UpdateDevices() hook removed\n");
		cleanup();
		return false;
	}

	return true;
}