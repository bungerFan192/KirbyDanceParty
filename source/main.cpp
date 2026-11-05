#include <3ds.h>
#include <citro3d.h>

#include <cstdio>
#include <cstring>

namespace {
constexpr u64 kPerfectWindowMs = 70;
constexpr u64 kGoodWindowMs = 145;
constexpr u64 kBeatMs = 650;
constexpr int kPhraseLength = 4;

enum class Direction : u8 {
    Up,
    Right,
    Down,
    Left
};

struct Beat {
    Direction direction;
    u64 targetMs;
};

const char* directionName(Direction direction) {
    switch (direction) {
        case Direction::Up: return "UP";
        case Direction::Right: return "RIGHT";
        case Direction::Down: return "DOWN";
        case Direction::Left: return "LEFT";
    }
    return "?";
}

u32 directionKey(Direction direction) {
    switch (direction) {
        case Direction::Up: return KEY_UP;
        case Direction::Right: return KEY_RIGHT;
        case Direction::Down: return KEY_DOWN;
        case Direction::Left: return KEY_LEFT;
    }
    return 0;
}

const char* judgementName(u64 difference) {
    if (difference <= kPerfectWindowMs) return "PERFECT!";
    if (difference <= kGoodWindowMs) return "GOOD!";
    return "MISS";
}

C3D_RenderTarget* topTarget = nullptr;

void drawTopScreen() {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_RenderTargetClear(topTarget, C3D_CLEAR_ALL, 0x251A45FF, 0);
    C3D_FrameDrawOn(topTarget);
    C3D_FrameEnd(0);
}

void drawBottomScreen(const Beat* phrase, int currentBeat, int score,
                      const char* status, const char* lastJudgement,
                      int combo, bool responsePhase) {
    consoleClear();
    printf("\x1b[1;1H KIRBY DANCE PARTY");
    printf("\x1b[3;1H------------------------------");
    printf("\x1b[5;1H%s", responsePhase ? "YOUR TURN! Repeat the phrase:" : "WATCH THE CALL:");
    for (int i = 0; i < kPhraseLength; ++i) {
        printf("\x1b[%d;3H%s %s", 7 + i * 2, i == currentBeat && responsePhase ? ">" : " ",
               directionName(phrase[i].direction));
    }
    printf("\x1b[16;1HScore: %d    Combo: %d", score, combo);
    printf("\x1b[18;1HLast: %s", lastJudgement);
    printf("\x1b[20;1H%s", status);
    printf("\x1b[23;1HD-Pad: dance    START: quit");
}

void runPrototype() {
    const Beat phrase[kPhraseLength] = {
        {Direction::Left, 0},
        {Direction::Up, kBeatMs},
        {Direction::Right, kBeatMs * 2},
        {Direction::Down, kBeatMs * 3}
    };

    int score = 0;
    int combo = 0;
    int beatIndex = 0;
    bool responsePhase = false;
    bool finished = false;
    bool judgedCurrentBeat = false;
    u64 phaseStart = osGetTime();
    u64 nextCallAt = phaseStart;
    char lastJudgement[24] = "READY";
    char status[64] = "Get ready...";

    while (aptMainLoop() && !finished) {
        hidScanInput();
        const u32 down = hidKeysDown();
        if (down & KEY_START) break;

        const u64 now = osGetTime();

        if (!responsePhase) {
            const u64 elapsed = now - phaseStart;
            if (elapsed >= kBeatMs * kPhraseLength + 500) {
                responsePhase = true;
                beatIndex = 0;
                judgedCurrentBeat = false;
                phaseStart = now + 700;
                std::strncpy(status, "Repeat the same four directions!", sizeof(status));
            } else {
                beatIndex = static_cast<int>(elapsed / kBeatMs);
                if (beatIndex >= kPhraseLength) beatIndex = kPhraseLength - 1;
                std::strncpy(status, "Listen and watch the call.", sizeof(status));
            }
        } else if (beatIndex < kPhraseLength) {
            const u64 target = phaseStart + phrase[beatIndex].targetMs;
            if (!judgedCurrentBeat) {
                const u32 expectedKey = directionKey(phrase[beatIndex].direction);
                const u32 pressed = down & (KEY_UP | KEY_RIGHT | KEY_DOWN | KEY_LEFT);
                if (pressed != 0 && now + kGoodWindowMs >= target &&
                    now <= target + kGoodWindowMs) {
                    const bool correct = (pressed & expectedKey) != 0;
                    const u64 difference = now > target ? now - target : target - now;
                    if (correct) {
                        const char* judgement = judgementName(difference);
                        std::strncpy(lastJudgement, judgement, sizeof(lastJudgement));
                        if (difference <= kPerfectWindowMs) score += 300;
                        else score += 100;
                        ++combo;
                    } else {
                        std::strncpy(lastJudgement, "MISS", sizeof(lastJudgement));
                        combo = 0;
                    }
                    judgedCurrentBeat = true;
                }
            }
            if (!judgedCurrentBeat && now > target + kGoodWindowMs) {
                std::strncpy(lastJudgement, "MISS", sizeof(lastJudgement));
                combo = 0;
                judgedCurrentBeat = true;
            }
            if (now > target + kBeatMs / 2) {
                ++beatIndex;
                judgedCurrentBeat = false;
            }
        } else {
            finished = true;
            std::strncpy(status, "Dance complete! Great job!", sizeof(status));
        }

        drawBottomScreen(phrase, beatIndex < kPhraseLength ? beatIndex : kPhraseLength - 1,
                         score, status, lastJudgement, combo, responsePhase);
        drawTopScreen();
    }

    consoleClear();
    printf("\x1b[1;1H KIRBY DANCE PARTY");
    printf("\x1b[5;3HPHRASE COMPLETE!");
    printf("\x1b[8;3HFinal score: %d", score);
    printf("\x1b[11;3HThanks for dancing!");
    printf("\x1b[15;1HPress A to return to HOME.");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_A) break;
        drawTopScreen();
    }
}
} // namespace

int main(int argc, char* argv[]) {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, nullptr);

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        gfxExit();
        return 1;
    }

    topTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!topTarget) {
        C3D_Fini();
        gfxExit();
        return 1;
    }
    C3D_RenderTargetSetOutput(topTarget, GFX_TOP, GFX_LEFT,
                              DISPLAY_TRANSFER_FLAGS);

    consoleClear();
    printf("KIRBY DANCE PARTY\n");
    printf("------------------------------\n");
    printf("A tiny rhythm prototype!\n\n");
    printf("Watch the four-step call, then\n");
    printf("repeat it with the D-Pad.\n\n");
    printf("Press A to start.\n");

    bool started = false;
    while (aptMainLoop() && !started) {
        hidScanInput();
        if (hidKeysDown() & KEY_A) started = true;
        if (hidKeysDown() & KEY_START) break;
        drawTopScreen();
    }

    if (started) runPrototype();

    C3D_RenderTargetDelete(topTarget);
    C3D_Fini();
    gfxExit();
    return 0;
}
