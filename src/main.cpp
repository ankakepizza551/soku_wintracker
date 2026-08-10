//
// SokuWinTracker - shows the cumulative 1P/2P match win count, only while in battle.
// Drawn as two small corner badges (top-left / top-right) so the center of the
// screen (weather banners, round announcements, the timer) stays clear.
//
#include <SokuLib.hpp>
#include <shlwapi.h>
#include <cstdio>

static void (SokuLib::BattleManager::*og_BattleManagerOnRender)();
static int (SokuLib::Title::*og_TitleOnProcess)();

struct Badge {
	SokuLib::DrawUtils::Sprite text;
	SokuLib::DrawUtils::RectangleShape shadow;
	SokuLib::DrawUtils::RectangleShape bg;
	SokuLib::DrawUtils::RectangleShape accent;
};

static bool s_fontLoaded = false;
static SokuLib::SWRFont s_font;
static Badge s_leftBadge;
static Badge s_rightBadge;
static char s_iniPath[MAX_PATH * 2];

static int s_p1Wins = 0;
static int s_p2Wins = 0;
static bool s_leftAwaitingReset = false;
static bool s_rightAwaitingReset = false;
static bool s_textDirty = true;
static SokuLib::BattleMode s_lastMainMode = (SokuLib::BattleMode)0xFF;
static bool s_armed = false;

// The tally is only meant to last for the current game session: every fresh
// launch of the exe (i.e. every call to Initialize) starts back at 0-0
// instead of restoring whatever was last written to the ini.
static void resetWins()
{
	s_p1Wins = 0;
	s_p2Wins = 0;
}

static void saveWins()
{
	char buf[16];

	wsprintfA(buf, "%d", s_p1Wins);
	WritePrivateProfileStringA("Wins", "P1", buf, s_iniPath);
	wsprintfA(buf, "%d", s_p2Wins);
	WritePrivateProfileStringA("Wins", "P2", buf, s_iniPath);
	s_textDirty = true;
}

static void initBadgeStyle(Badge &badge, SokuLib::DrawUtils::DxSokuColor accentColor)
{
	badge.shadow.setFillColor(SokuLib::DrawUtils::DxSokuColor(0, 0, 0, 110));
	badge.shadow.setBorderColor(SokuLib::DrawUtils::DxSokuColor(0, 0, 0, 0));

	badge.bg.setFillColor(SokuLib::DrawUtils::DxSokuColor(12, 14, 24, 195));
	badge.bg.setBorderColor(SokuLib::DrawUtils::DxSokuColor(230, 190, 110, 220));

	badge.accent.setFillColor(accentColor);
	SokuLib::DrawUtils::DxSokuColor noBorder = accentColor;
	noBorder.a = 0;
	badge.accent.setBorderColor(noBorder);
}

static void createFont()
{
	if (s_fontLoaded)
		return;

	SokuLib::FontDescription desc;

	desc.r1 = 255;
	desc.g1 = 255;
	desc.b1 = 255;
	desc.r2 = 255;
	desc.g2 = 255;
	desc.b2 = 255;
	desc.height = 20;
	desc.weight = FW_BOLD;
	desc.italic = 0;
	desc.shadow = 3;
	desc.bufferSize = 100000;
	desc.charSpaceX = 0;
	desc.charSpaceY = 0;
	desc.offsetX = 0;
	desc.offsetY = 0;
	desc.useOffset = 0;
	strcpy(desc.faceName, "MonoSpatialModSWR");
	s_font.create();
	s_font.setIndirect(desc);

	initBadgeStyle(s_leftBadge, SokuLib::DrawUtils::DxSokuColor(70, 150, 255, 230));   // 1P: blue
	initBadgeStyle(s_rightBadge, SokuLib::DrawUtils::DxSokuColor(255, 90, 130, 230));  // 2P: pink

	s_fontLoaded = true;
}

// left = true -> badge anchored to the top-left corner, growing rightward.
// left = false -> badge anchored to the top-right corner, growing leftward.
static void layoutBadge(Badge &badge, const char *text, bool left)
{
	SokuLib::Vector2<int> realSize;

	if (!badge.text.texture.createFromText(text, s_font, {0x400, 40}, &realSize))
		return;
	badge.text.setSize(realSize.to<unsigned>());
	badge.text.rect.width = realSize.x;
	badge.text.rect.height = realSize.y;

	const int padX = 12;
	const int padY = 6;
	const int panelW = realSize.x + padX * 2;
	const int panelH = realSize.y + padY * 2;
	const int marginX = 10;
	const int panelY = 24; // just under the health bars, level with the gear/timer

	int panelX = left ? marginX : (640 - marginX - panelW);

	badge.text.setPosition({panelX + padX, panelY + padY});

	badge.shadow.setSize({(unsigned)panelW, (unsigned)panelH});
	badge.shadow.setPosition({panelX + 3, panelY + 4});

	badge.bg.setSize({(unsigned)panelW, (unsigned)panelH});
	badge.bg.setPosition({panelX, panelY});

	const int accentW = 4;
	badge.accent.setSize({(unsigned)accentW, (unsigned)panelH});
	badge.accent.setPosition({left ? panelX : (panelX + panelW - accentW), panelY});
}

static void updateBadges()
{
	char leftText[32];
	char rightText[32];

	wsprintfA(leftText, "1P  %d", s_p1Wins);
	wsprintfA(rightText, "2P  %d", s_p2Wins);
	layoutBadge(s_leftBadge, leftText, true);
	layoutBadge(s_rightBadge, rightText, false);
	s_textDirty = false;
}

static bool isNetBattleMode(SokuLib::BattleMode mode)
{
	return mode == SokuLib::BATTLE_MODE_VSCLIENT || mode == SokuLib::BATTLE_MODE_VSSERVER
		|| mode == SokuLib::BATTLE_MODE_VSWATCH;
}

static void drawBadge(const Badge &badge)
{
	badge.shadow.draw();
	badge.bg.draw();
	badge.accent.draw();
	badge.text.draw();
}

// Called once per real displayed frame, even under rollback netcode (e.g. giuroll),
// which can invoke BattleManager::onProcess several times per real frame to
// resimulate/catch up. Any state that must be evaluated exactly once per real
// frame (win tally, opponent-change detection, ini writes) belongs here rather
// than in onProcess, otherwise it gets double-counted or observes torn state
// mid-resimulation.
void __fastcall BattleOnRender(SokuLib::BattleManager *This)
{
	(This->*og_BattleManagerOnRender)();

	if (SokuLib::subMode == SokuLib::BATTLE_SUBMODE_REPLAY)
		return; // don't touch the tally while watching a replay
	if (SokuLib::mainMode == SokuLib::BATTLE_MODE_PRACTICE)
		return; // practice mode has no meaningful win tally, stay hidden

	// mainMode can still read as the previous mode for a few rendered frames
	// while a new battle is being set up, and practice mode leaves the score
	// fields holding its own placeholder values (seen as e.g. 1/2) instead of
	// 0/0 during that window. Require observing an actual 0-0 after every
	// mode change before trusting scores again, so those leftover values
	// never get counted or drawn.
	//
	// Crossing the net/local boundary also resets the tally, in either
	// direction: entering a net battle mode (VSCLIENT/VSSERVER/VSWATCH) is
	// treated as a fresh opponent, since reliably telling "same opponent, new
	// match" apart from "different opponent" from the netcode's internal
	// state isn't possible without relying on undocumented/fragile memory
	// layouts. Leaving a net battle mode back to local/CPU (or the menu, seen
	// here as whatever mode is entered next) resets too, so an online tally
	// never bleeds into an unrelated local session. Switching between local/
	// CPU modes themselves doesn't reset, they just keep accumulating for the
	// session.
	if (SokuLib::mainMode != s_lastMainMode) {
		bool crossedNetBoundary = isNetBattleMode(SokuLib::mainMode) != isNetBattleMode(s_lastMainMode);

		s_lastMainMode = SokuLib::mainMode;
		s_armed = false;
		if (crossedNetBoundary) {
			s_p1Wins = 0;
			s_p2Wins = 0;
			s_leftAwaitingReset = false;
			s_rightAwaitingReset = false;
			saveWins();
		}
	}

	char leftScore = This->leftCharacterManager.score;
	char rightScore = This->rightCharacterManager.score;

	if (!s_armed) {
		if (leftScore != 0 || rightScore != 0)
			return;
		s_armed = true;
	}

	createFont();

	if (leftScore == 0 && rightScore == 0) {
		// Both sides back to 0-0: a new match has started, allow counting again.
		s_leftAwaitingReset = false;
		s_rightAwaitingReset = false;
	}

	// First to 2 round wins takes the match.
	if (leftScore >= 2 && !s_leftAwaitingReset) {
		s_p1Wins++;
		s_leftAwaitingReset = true;
		saveWins();
	}
	if (rightScore >= 2 && !s_rightAwaitingReset) {
		s_p2Wins++;
		s_rightAwaitingReset = true;
		saveWins();
	}

	if (s_textDirty)
		updateBadges();
	drawBadge(s_leftBadge);
	drawBadge(s_rightBadge);
}

// Reaching the title screen means the player has backed all the way out of
// whatever battle mode they were in, so the tally no longer applies to
// anything ongoing. mainMode/subMode alone can't catch this: mainMode keeps
// whatever battle type was last played (e.g. still BATTLE_MODE_ARCADE) even
// after returning to the menu, so re-entering the same mode later looked
// like "no mode change" and the count survived across menu trips.
int __fastcall Title_OnProcess(SokuLib::Title *This)
{
	int ret = (This->*og_TitleOnProcess)();

	if (s_p1Wins != 0 || s_p2Wins != 0) {
		resetWins();
		s_leftAwaitingReset = false;
		s_rightAwaitingReset = false;
		s_armed = false;
		s_lastMainMode = (SokuLib::BattleMode)0xFF;
		saveWins();
	}
	return ret;
}

extern "C" __declspec(dllexport) bool CheckVersion(const BYTE hash[16])
{
	return memcmp(hash, SokuLib::targetHash, 16) == 0;
}

extern "C" __declspec(dllexport) bool Initialize(HMODULE hMyModule, HMODULE hParentModule)
{
	DWORD old;

	GetModuleFileNameA(hMyModule, s_iniPath, MAX_PATH);
	PathRemoveFileSpecA(s_iniPath);
	PathAppendA(s_iniPath, "SokuWinTracker.ini");
	resetWins();
	saveWins();

	VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, PAGE_EXECUTE_WRITECOPY, &old);
	og_BattleManagerOnRender = SokuLib::TamperDword(&SokuLib::VTable_BattleManager.onRender, BattleOnRender);
	og_TitleOnProcess = SokuLib::TamperDword(&SokuLib::VTable_Title.onProcess, Title_OnProcess);
	VirtualProtect((PVOID)RDATA_SECTION_OFFSET, RDATA_SECTION_SIZE, old, &old);

	FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
	return true;
}

extern "C" int APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID lpReserved)
{
	return TRUE;
}
