#include "main.h"

hl::StaticInit<class SmashMain> g_main;

hl::ConsoleEx m_con;

void InitDiscord();
void cleanupPlayerPtrs();
void cleanup();
void UpdateMenuPresence();
void UpdateMatchPresence();
bool checkState(uint16_t i);
void updateEntities();
void updatePlayerPtrs();
void updatePtrs();

uintptr_t cache_invalidate = NULL;
uintptr_t g_serialupdate = NULL;
uintptr_t g_serialshutdown = NULL;

uintptr_t memlo = NULL;
uintptr_t nametagRegion = NULL;
uintptr_t patchStart = NULL;
uintptr_t firstEntity = NULL;
uintptr_t timer = NULL;

typedef uint8_t*(__stdcall *getptr)(uint32_t addr);
getptr mem_getptr;

void hkSerialUpdate(hl::CpuContext *ctx);
void hkSerialShutdown(hl::CpuContext *ctx);

uint32_t numPlayers = 0;
bool menu = true;

struct Entity
{
	uintptr_t entity_addr;
	char* player_addr;
	unsigned char player_mem[0x2384];
	unsigned char saved_mem[0x2384];
};
Entity entity[6] = { NULL, NULL, NULL, NULL, NULL, NULL };

//16 _byteswap_ushort(unsigned short value);
//32 _byteswap_ulong(unsigned long value);
//64 _byteswap_uint64(unsigned __int64 value);

bool SmashMain::init() {
	m_con.create("SmashData");

	cache_invalidate = hl::FindPattern("40 56 48 83 EC 20 F7 05 ?? ?? ?? ?? 00 80 00 00 8B F2 0F 84 67 03 00 00 48 89 5C 24 30");
	if (!cache_invalidate) {
		m_con.printf("[Core::Init] PPCCache::Invalidate pattern invalid\n");
		return false;
	}
	m_con.printf("[Core::Init] PowerPC::InstructionCache::Invalidate(): %p\n", cache_invalidate);

	mem_getptr = (getptr)hl::FindPattern("48 83 EC 38 81 E1 FF FF FF 3F 81 F9 00 00 80 01 73 0E 8B C1");
	if (!mem_getptr) {
		m_con.printf("[Core::Init] Memory::GetPointer pattern invalid\n");
		return false;
	}
	m_con.printf("[Core::Init] Memory_GetPointer(): %p\n", mem_getptr);

	g_serialupdate = hl::FindPattern("48 83 EC 28 48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ??");
	if (!g_serialupdate) {
		m_con.printf("[Core::Init] SerialInterface::UpdateDevices() pattern invalid\n");
		return false;
	}
	m_con.printf("[Core::Init] SerialInterface::UpdateDevices(): %p\n", g_serialupdate);

	m_serialupdate = m_hooker.hookDetour(g_serialupdate + 0x2b, 15, hkSerialUpdate);
	if (m_serialupdate == nullptr) {
		m_con.printf("[Core::Init] SerialInterface::UpdateDevices() hook failed\n");
		return false;
	}

	g_serialshutdown = hl::FindPattern("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 33 FF 48 8D 35 ?? ?? ?? ?? 8B DF 48 83 FB 60 73 35");
	if (!g_serialshutdown) {
		m_con.printf("[Core::Init] SerialInterface::Shutdown() pattern invalid\n");
		return false;
	}
	m_con.printf("[Core::Init] SerialInterface::Shutdown(): %p\n", g_serialshutdown);

	m_serialshutdown = m_hooker.hookDetour(g_serialshutdown, 15, hkSerialShutdown);
	if (m_serialshutdown == nullptr) {
		m_con.printf("[Core::Init] SerialInterface::Shutdown() hook failed\n");
	}

	cleanupPlayerPtrs();
	InitDiscord();

	return true;
}

/* Step occurs every 10ms, so don't rely on this for sending packets on frame.
*/
bool SmashMain::step() {
	if (GetAsyncKeyState(VK_END) < 0) {
		m_hooker.unhook(m_serialupdate);
		m_hooker.unhook(m_serialshutdown);
		cleanup();
		Discord_Shutdown();
		return false;
	}
	if (GetAsyncKeyState(VK_HOME)) {
		for (uint16_t i = 0; i < 6; i++) {
			if (checkState(i)) {
				memcpy(entity[i].saved_mem, entity[i].player_mem, 0x2384);
			}
		}
		Sleep(400);
	}
	if (GetAsyncKeyState(VK_INSERT)) {
		for (uint16_t i = 0; i < 6; i++) {
			if (checkState(i)) {
				memcpy(entity[i].player_addr, entity[i].saved_mem, 0x55C);
				memcpy(entity[i].player_addr + 0x610, entity[i].saved_mem + 0x610, 0x1364);
				memcpy(entity[i].player_addr + 0x1978, entity[i].saved_mem + 0x1978, 0x884);
				memcpy(entity[i].player_addr + 0x2200, entity[i].saved_mem + 0x2200, 0x184);
			}
		}
		Sleep(400);
	}
	return true;
}

uint8_t* getPointer(uint32_t address) {
	return mem_getptr(address);
}

void hkSerialUpdate(hl::CpuContext *ctx) {
	if (memlo != NULL) {
		uint32_t addr = *reinterpret_cast<uint32_t*>(getPointer(0x8065CC14));
		if (_byteswap_ulong(addr) == 0x8111F880) {
			if (numPlayers > 0) {
				cleanupPlayerPtrs();
				menu = true;
			}
			if (menu) {
				UpdateMenuPresence();
				menu = false;
			}
			return;
		}

		addr = *reinterpret_cast<uint32_t*>(timer);
		if (addr != 0) {
			addr = *reinterpret_cast<uint32_t*>(firstEntity);
			if (_byteswap_ulong(addr) > 0x80000000) {
				if (numPlayers > 0) {
					updateEntities();
				}
				else {
					updatePlayerPtrs();
					UpdateMatchPresence();
				}
			}
		}
	}
	else {
		updatePtrs();
	}
}

void hkSerialShutdown(hl::CpuContext *ctx) {
	if(memlo != NULL)
		UpdateMenuPresence();
		cleanup();
}

void cleanupPlayerPtrs() {
	numPlayers = 0;
	for (uint16_t i = 0; i < 6; i++) {
		entity[i].entity_addr = NULL;
		entity[i].player_addr = NULL;
		memset(entity[i].player_mem, 0, 0x2384);
		memset(entity[i].saved_mem, 0, 0x2384);
	}
	m_con.printf("[Core::Cleanup] Invalidated Entities\n");
}

void cleanup() {
	memlo = NULL;
	nametagRegion = NULL;
	patchStart = NULL;
	firstEntity = NULL;
	timer = NULL;
	cleanupPlayerPtrs();
	m_con.printf("[Core::Cleanup] Invalidated VMEM Hooks\n");
}

void updatePtrs() {
	memlo = (uintptr_t)getPointer(0x80000000);

	//Memory is going to be contiguous with memlo, but should probably just make a habit of checking w/ the function
	nametagRegion = (uintptr_t)getPointer(0x8045D850); //Start of the nametag list for writing

	patchStart = (uintptr_t)getPointer(0x8045D930); //This is the free region after nametags for say, a buffer overflow

	firstEntity = (uintptr_t)getPointer(0x80BDA4A0); //This is a location the game stores a ptr to the first loaded entity

	timer = (uintptr_t)getPointer(0x8046B6C4);
	m_con.printf("[Core::Ptrs] Pointers loaded: %p, %p, %p, %p, %p\n", memlo, nametagRegion, patchStart, firstEntity, timer);
}

void updatePlayerPtrs() {
	uint32_t addr = *reinterpret_cast<uint32_t*>(firstEntity);
	if (_byteswap_ulong(addr) > 0x80000000) {
		for (int i = 0; i < 6; i++) {
			if (i > 0)
				addr = *reinterpret_cast<uint32_t*>(entity[i - 1].entity_addr + 0x08); //Next entity is entity ptr + 0x08
			if (_byteswap_ulong(addr) < 0x80000000)
				break;
			entity[i].entity_addr = (uintptr_t)getPointer(_byteswap_ulong(addr));
			m_con.printf("[INFO] Entity %d MEM1: %p\n", i + 1, _byteswap_ulong(addr));

			addr = *reinterpret_cast<uint32_t*>(entity[i].entity_addr + 0x2C); //Now we'll jump to their player struct
			entity[i].player_addr = reinterpret_cast<char*>((uintptr_t)getPointer(_byteswap_ulong(addr)));
			numPlayers += 1;
		}
	}
}

void updateEntities() {
	for (uint16_t i = 0; i < 6; i++) {
		if (checkState(i)) {
			memcpy(&entity[i].player_mem, entity[i].player_addr, 0x2384);
		}
	}
}

bool checkState(uint16_t i) {
	if (entity[i].player_addr != NULL) {
		uint32_t chid = *reinterpret_cast<uint32_t*>(entity[i].player_addr + 0x04); //Load the character ID
		uint32_t as = *reinterpret_cast<uint32_t*>(entity[i].player_addr + 0x10); //Load the action state

		switch (_byteswap_ulong(chid)) {
		case 0xD: //Samus
			if (*reinterpret_cast<uint32_t*>(entity[i].player_addr + 0x223c) != 0) { //Grappled Entity
				return false;
			}
			break;

		case 0x6:
		case 0x14:
			if (*reinterpret_cast<uint32_t*>(entity[i].player_addr + 0x2238) != 0) { //Grappled Entity
				return false;
			}
			break;

		default:
			break;
		}

		switch (_byteswap_ulong(as)) {
		case 0xB: //"Sleep"
		case 0xC: //Rebirth
		case 0xD: //RebirthWait
		case 0xFD: //LedgeGrab
		case 0x142: //Entry
		case 0x143: //EntryStart
		case 0x144: //EntryEnd
			return false;

		default:
			break;
		}
	}
	else {
		return false;
	}
	return true;
}


void handleDiscordReady() {
	m_con.printf("[DISCORD] Ready\n");
}

void handleDiscordError(int errorcode, const char* message) {
	m_con.printf("[DISCORD] Error: %s\n", message);
}

void handleDiscordDisconnected(int errorcode, const char* message) {
	m_con.printf("[DISCORD] Disconnect: %s\n", message);
}

void handleDiscordJoinGame(const char* joinSecret) {

}

void handleDiscordSpectateGame(const char* specSecret) {

}

void handleDiscordJoinRequest(const DiscordJoinRequest* request) {

}

void InitDiscord() {
	const char* appid = "414213427721142273";

	DiscordEventHandlers handlers;
	memset(&handlers, 0, sizeof(handlers));
	handlers.ready = handleDiscordReady;
	handlers.errored = handleDiscordError;
	handlers.disconnected = handleDiscordDisconnected;
	handlers.joinGame = handleDiscordJoinGame;
	handlers.spectateGame = handleDiscordSpectateGame;
	handlers.joinRequest = handleDiscordJoinRequest;
	Discord_Initialize(appid, &handlers, 1, NULL);
	m_con.printf("[DISCORD] Initialized\n");
}

void UpdateMenuPresence()
{
	char buffer[256];
	DiscordRichPresence discordPresence;
	memset(&discordPresence, 0, sizeof(discordPresence));
	discordPresence.state = "Selecting Characters";
	sprintf(buffer, "Waiting in Lobby");
	discordPresence.details = buffer;
	discordPresence.startTimestamp = time(0);
	discordPresence.largeImageKey = "melee";
	discordPresence.smallImageKey = "melee_small";
	Discord_UpdatePresence(&discordPresence);
}


void UpdateMatchPresence()
{
	char buffer[256];
	DiscordRichPresence discordPresence;
	memset(&discordPresence, 0, sizeof(discordPresence));
	discordPresence.state = "In Match";
	sprintf(buffer, "MELEE");
	discordPresence.details = buffer;
	discordPresence.startTimestamp = time(0);
	discordPresence.largeImageKey = "melee";
	discordPresence.smallImageKey = "melee_small";
	Discord_UpdatePresence(&discordPresence);
}