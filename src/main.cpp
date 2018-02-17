#include "main.h"

hl::StaticInit<class SmashMain> g_main;
hl::ConsoleEx m_con;
hl::Timer timer;

void InitDiscord();
void cleanupPlayerPtrs();
void cleanup();
void UpdateMenuPresence();
void UpdateMatchPresence();
bool checkState(uint16_t i);
void updateEntities();
void updatePlayerPtrs();
char* getCurrentStage();
uint32_t getGameTime();
void updatePtrs();

uintptr_t cache_invalidate = NULL;
uintptr_t g_serialupdate = NULL;
uintptr_t g_serialshutdown = NULL;

typedef uint8_t*(__stdcall *getptr)(uint32_t addr);
getptr mem_getptr;

void hkSerialUpdate(hl::CpuContext *ctx);
void hkSerialShutdown(hl::CpuContext *ctx);

uintptr_t memlo = NULL;

#define MEM1 0x80000000
#define NAMETAG_REGION 0x8045D850
#define PATCH_START 0x8045D930
#define FIRST_ENTITY 0x80BDA4A0
#define FRAME_TIMER 0x8046B6C4
#define CURR_MENU 0x8065CC14
#define CS_MELEE 0x8111F880

uint32_t numPlayers = 0;
bool menu = true;

struct Entity
{
	uintptr_t entity_addr;
	uintptr_t player_addr;
	unsigned char player_mem[0x2384];
	unsigned char saved_mem[0x2384];
};
Entity entity[8];

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
	/*if (GetAsyncKeyState(VK_HOME)) {
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
	}*/
	return true;
}

uint8_t* getPointer(uint32_t address) {
	return mem_getptr(address);
}

void hkSerialUpdate(hl::CpuContext *ctx) {
	if (memlo != NULL) {
		uint32_t addr = *(uint32_t*)(getPointer(CURR_MENU));
		if (_byteswap_ulong(addr) == CS_MELEE) {
			if (numPlayers > 0) {
				cleanupPlayerPtrs();
				menu = true;
			}
			if (menu || timer.diff() > 17.0f) {
				UpdateMenuPresence();
				timer.reset();
				menu = false;
			}
			return;
		}

		addr = *(uint32_t*)(getPointer(FRAME_TIMER));
		if (addr != 0) {
			addr = *(uint32_t*)(getPointer(FIRST_ENTITY));
			if (_byteswap_ulong(addr) > MEM1) {
				if (numPlayers > 0) {
					updateEntities();
					if (!menu && timer.diff() > 17.0f) {
						UpdateMatchPresence();
						timer.reset();
					}
				}
				else {
					updatePlayerPtrs();
					UpdateMatchPresence();
					timer.reset();
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
	for (uint16_t i = 0; i < 8; i++) {
		entity[i].entity_addr = NULL;
		entity[i].player_addr = NULL;
		memset(entity[i].player_mem, 0, 0x2384);
		memset(entity[i].saved_mem, 0, 0x2384);
	}
	m_con.printf("[Core::Cleanup] Invalidated Entities\n");
}

void cleanup() {
	memlo = NULL;
	cleanupPlayerPtrs();
	m_con.printf("[Core::Cleanup] Invalidated VMEM Hooks\n");
	Discord_Shutdown();
	m_con.printf("[DISCORD] Shutdown\n");
}

void updatePtrs() {
	memlo = (uintptr_t)getPointer(MEM1);

	//Memory is going to be contiguous with memlo, but should probably just make a habit of checking w/ the function

	m_con.printf("[Core::Ptrs] MEM1 Pointer: %p\n", memlo);
	InitDiscord();
}

void updatePlayerPtrs() {
	uint32_t addr = *(uint32_t*)(getPointer(FIRST_ENTITY));
	if (_byteswap_ulong(addr) > MEM1) {
		for (int i = 0; i < 8; i++) {
			if (i > 0)
				addr = *(uint32_t*)(entity[i - 1].entity_addr + 0x08); //Next entity is entity ptr + 0x08
			if (_byteswap_ulong(addr) < MEM1)
				break;
			entity[i].entity_addr = (uintptr_t)getPointer(_byteswap_ulong(addr));
			m_con.printf("[INFO] Entity %d MEM1: %p\n", i + 1, _byteswap_ulong(addr));

			addr = *(uint32_t*)(entity[i].entity_addr + 0x2C); //Now we'll jump to their player struct
			entity[i].player_addr = (uintptr_t)getPointer(_byteswap_ulong(addr));
			numPlayers += 1;
		}
	}
}

void updateEntities() {
	for (uint16_t i = 0; i < 8; i++) {
		if (checkState(i)) {
			memcpy(&entity[i].player_mem, (char*)entity[i].player_addr, 0x2384);
		}
	}
}

uint32_t getGameTime() {
	return _byteswap_ulong(*(uint32_t*)(getPointer(0x8046B6C8)));
}

char* getCurrentStage() {
	uint32_t stageid = _byteswap_ulong(*(uint32_t*)(getPointer(0x804D49E8)));
	switch (stageid) {
	case 0x2:
		return "fountain";
	case 0x3:
		return "poke_stadium";
	case 0x8:
		return "yoshi_story";
	case 0x1C:
		return "dream_land";
	case 0x1F:
		return "battlefield";
	case 0x20:
		return "final_destination";
	default:
		return "melee";
	}
}

bool checkState(uint16_t i) {
	if (entity[i].player_addr != NULL) {
		uint32_t chid = *(uint32_t*)(entity[i].player_addr + 0x04); //Load the character ID
		uint32_t as = *(uint32_t*)(entity[i].player_addr + 0x10); //Load the action state

		switch (_byteswap_ulong(chid)) {
		case 0xD: //Samus
			if (*(uint32_t*)(entity[i].player_addr + 0x223c) != 0) { //Grappled Entity
				return false;
			}
			break;

		case 0x6:
		case 0x14:
			if (*(uint32_t*)(entity[i].player_addr + 0x2238) != 0) { //Grappled Entity
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
		case 0x144: //EntryEnds
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
	discordPresence.endTimestamp = time(0) + getGameTime();
	discordPresence.largeImageKey = getCurrentStage();
	discordPresence.smallImageKey = "melee_small";
	Discord_UpdatePresence(&discordPresence);
}