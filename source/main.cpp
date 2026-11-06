#include <3ds.h>
#include <3ds/services/csnd.h>

#include <cstdio>
#include <cstring>
#include <cmath>

namespace {
constexpr u64 kPerfectWindowMs = 70;
constexpr u64 kGoodWindowMs = 145;
constexpr u64 kBeatMs = 650;
constexpr int kPhraseLength = 4;
constexpr int kSampleRate = 22050;
constexpr int kToneSamples = 2205;

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

// -----------------------------------------------------------------------------
// Tiny software-drawn rhythm stage. gfx framebuffers are stored rotated on 3DS;
// this helper maps a landscape 400x240 coordinate to the framebuffer layout.
// -----------------------------------------------------------------------------
u8* topFramebuffer = nullptr;
u16 framebufferWidth = 0;
u16 framebufferHeight = 0;
s16* toneBuffers[4] = {nullptr, nullptr, nullptr, nullptr};
bool audioReady = false;
int nextAudioChannel = 0;

struct Color {
    u8 r, g, b;
};

void pixel(int x, int y, Color color) {
    // The 3DS framebuffer is physically 240x400 and stored column-major.
    // Our drawing helpers use landscape coordinates (400x240), so rotate
    // the coordinate when calculating the byte offset.
    if (!topFramebuffer || x < 0 || x >= 400 || y < 0 || y >= 240 ||
        framebufferWidth < 240 || framebufferHeight < 400) return;

    const unsigned int fbX = static_cast<unsigned int>(239 - y);
    const unsigned int fbY = static_cast<unsigned int>(x);
    const unsigned int offset =
        (fbY * static_cast<unsigned int>(framebufferWidth) + fbX) * 3u;

    // GFX_BGR8_OES stores blue, green, red.
    topFramebuffer[offset + 0] = color.b;
    topFramebuffer[offset + 1] = color.g;
    topFramebuffer[offset + 2] = color.r;
}

void fillRect(int x, int y, int w, int h, Color color) {
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px)
            pixel(px, py, color);
}

void line(int x0, int y0, int x1, int y1, Color color) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

void circle(int cx, int cy, int radius, Color color) {
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius)
                pixel(cx + x, cy + y, color);
        }
    }
}

void drawArrow(int cx, int cy, Direction direction, Color color) {
    // Draw a chunky arrow using a shaft and two diagonal strokes.
    int dx = 0, dy = 0;
    switch (direction) {
        case Direction::Up: dy = -1; break;
        case Direction::Right: dx = 1; break;
        case Direction::Down: dy = 1; break;
        case Direction::Left: dx = -1; break;
    }
    const int tx = cx + dx * 9;
    const int ty = cy + dy * 9;
    line(cx - dx * 5, cy - dy * 5, tx, ty, color);
    line(tx, ty, tx - dx * 7 - dy * 6, ty - dy * 7 + dx * 6, color);
    line(tx, ty, tx - dx * 7 + dy * 6, ty - dy * 7 - dx * 6, color);
    // Thicken the shaft.
    for (int n = -1; n <= 1; ++n) {
        if (dx != 0) line(cx - dx * 5, cy + n, tx, ty + n, color);
        else line(cx + n, cy - dy * 5, tx + n, ty, color);
    }
}

void drawNote(int cx, int cy, Direction direction, bool active, bool completed) {
    const Color shadow{25, 18, 55};
    const Color border{255, 255, 255};
    const Color face = completed ? Color{94, 220, 164} :
                       (active ? Color{255, 213, 92} : Color{104, 190, 245});
    fillRect(cx - 17 + 3, cy - 17 + 4, 34, 34, shadow);
    fillRect(cx - 17, cy - 17, 34, 34, border);
    fillRect(cx - 14, cy - 14, 28, 28, face);
    drawArrow(cx, cy, direction, Color{40, 35, 70});
}

void drawRhythmStage(bool responsePhase, int beatIndex, float progress,
                     const Beat* phrase, const char* judgement) {
    u16 width = 0, height = 0;
    topFramebuffer = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &width, &height);
    framebufferWidth = width;
    framebufferHeight = height;
    if (!topFramebuffer || width < 240 || height < 400) return;

    const Color bg{66, 43, 112};
    const Color panel{91, 65, 145};
    const Color panelDark{48, 36, 91};
    const Color lineColor{238, 226, 255};
    const Color pink{255, 133, 190};
    const Color yellow{255, 220, 112};
    const int noteX[kPhraseLength] = {64, 155, 246, 337};

    fillRect(0, 0, 400, 240, bg);

    // Bright stage framing and two call/response lanes.
    fillRect(12, 12, 376, 216, panel);
    fillRect(20, 20, 360, 200, panelDark);
    fillRect(30, 42, 340, 70, panel);
    fillRect(30, 132, 340, 70, panel);
    line(40, 78, 360, 78, lineColor);
    line(40, 168, 360, 168, lineColor);

    // Four beat markers per lane, like a little rap-score strip.
    for (int i = 0; i < kPhraseLength; ++i) {
        const bool current = (i == beatIndex);
        drawNote(noteX[i], 78, phrase[i].direction,
                 !responsePhase && current, false);
        drawNote(noteX[i], 168, phrase[i].direction,
                 responsePhase && current, responsePhase && i < beatIndex);
        circle(noteX[i], 112, 2, yellow);
        circle(noteX[i], 202, 2, pink);
    }

    // A moving marker travels along the active line during each phrase.
    const int markerX = 48 + static_cast<int>(progress * 304.0f);
    const int markerY = responsePhase ? 142 : 52;
    circle(markerX, markerY, 8, yellow);
    circle(markerX, markerY, 4, Color{255, 255, 255});
    line(markerX, markerY + 9, markerX, responsePhase ? 158 : 68, yellow);

    // A simple status lamp gives instant visual feedback.
    Color statusColor{150, 190, 255};
    if (std::strcmp(judgement, "PERFECT!") == 0) statusColor = Color{112, 245, 174};
    else if (std::strcmp(judgement, "GOOD!") == 0) statusColor = Color{255, 220, 112};
    else if (std::strcmp(judgement, "MISS") == 0) statusColor = Color{255, 112, 132};
    circle(365, 30, 7, statusColor);
}

void presentFrame() {
    gfxFlushBuffers();
    gfxSwapBuffers();
}

void playTone(float frequency, int channel, float volume = 0.35f) {
    if (!audioReady || channel < 0 || channel >= 4 || !toneBuffers[channel]) return;
    s16* samples = toneBuffers[channel];
    const float twoPi = 6.28318530718f;
    for (int i = 0; i < kToneSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float envelope = 1.0f - static_cast<float>(i) / kToneSamples;
        const float wave = std::sin(twoPi * frequency * t);
        samples[i] = static_cast<s16>(wave * envelope * 10500.0f);
    }
    CSND_FlushDataCache(samples, kToneSamples * sizeof(s16));
    csndPlaySound(channel, SOUND_FORMAT_16BIT | SOUND_ONE_SHOT,
                  kSampleRate, volume, 0.0f, samples, nullptr,
                  kToneSamples * sizeof(s16));
}

void playCue(Direction direction) {
    float frequency = 440.0f;
    switch (direction) {
        case Direction::Up: frequency = 660.0f; break;
        case Direction::Right: frequency = 784.0f; break;
        case Direction::Down: frequency = 392.0f; break;
        case Direction::Left: frequency = 523.0f; break;
    }
    const int channel = nextAudioChannel++ % 4;
    playTone(frequency, channel, 0.28f);
}

void playJudgement(const char* judgement) {
    if (std::strcmp(judgement, "PERFECT!") == 0) {
        playTone(988.0f, nextAudioChannel++ % 4, 0.4f);
    } else if (std::strcmp(judgement, "GOOD!") == 0) {
        playTone(659.0f, nextAudioChannel++ % 4, 0.32f);
    } else {
        playTone(180.0f, nextAudioChannel++ % 4, 0.32f);
    }
}

bool initAudio() {
    if (R_FAILED(csndInit())) return false;
    for (int i = 0; i < 4; ++i) {
        toneBuffers[i] = static_cast<s16*>(linearAlloc(kToneSamples * sizeof(s16)));
        if (!toneBuffers[i]) {
            for (int j = 0; j < i; ++j) {
                linearFree(toneBuffers[j]);
                toneBuffers[j] = nullptr;
            }
            csndExit();
            return false;
        }
    }
    audioReady = true;
    return true;
}

void shutdownAudio() {
    if (!audioReady) return;
    for (int i = 0; i < 4; ++i) {
        if (toneBuffers[i]) linearFree(toneBuffers[i]);
        toneBuffers[i] = nullptr;
    }
    csndExit();
    audioReady = false;
}

void drawBottomScreen(const Beat* phrase, int currentBeat, int score,
                      const char* status, const char* lastJudgement,
                      int combo, bool responsePhase) {
    consoleClear();
    printf("\x1b[1;1H KIRBY DANCE PARTY");
    printf("\x1b[3;1H------------------------------");
    printf("\x1b[5;1H%s", responsePhase ? "YOUR TURN! Repeat the phrase:" : "WATCH THE CALL:");
    for (int i = 0; i < kPhraseLength; ++i) {
        printf("\x1b[%d;3H%s %s", 7 + i * 2,
               i == currentBeat && responsePhase ? ">" : " ",
               directionName(phrase[i].direction));
    }
    printf("\x1b[16;1HScore: %d    Combo: %d", score, combo);
    printf("\x1b[18;1HLast: %s", lastJudgement);
    printf("\x1b[20;1H%s", status);
    printf("\x1b[23;1H[ D-PAD ] DANCE     [ START ] QUIT");
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
    int lastCallBeat = -1;
    bool responsePhase = false;
    bool finished = false;
    bool judgedCurrentBeat = false;
    u64 phaseStart = osGetTime();
    char lastJudgement[24] = "READY";
    char status[64] = "Get ready...";

    while (aptMainLoop() && !finished) {
        hidScanInput();
        const u32 down = hidKeysDown();
        if (down & KEY_START) break;

        const u64 now = osGetTime();
        float progress = 0.0f;

        if (!responsePhase) {
            const u64 elapsed = now - phaseStart;
            if (elapsed >= kBeatMs * kPhraseLength + 500) {
                responsePhase = true;
                beatIndex = 0;
                judgedCurrentBeat = false;
                phaseStart = now + 700;
                std::strncpy(status, "Repeat the same four directions!", sizeof(status));
                playTone(330.0f, nextAudioChannel++ % 4, 0.22f);
            } else {
                beatIndex = static_cast<int>(elapsed / kBeatMs);
                if (beatIndex >= kPhraseLength) beatIndex = kPhraseLength - 1;
                if (beatIndex != lastCallBeat) {
                    lastCallBeat = beatIndex;
                    playCue(phrase[beatIndex].direction);
                }
                progress = static_cast<float>(elapsed) /
                           static_cast<float>(kBeatMs * kPhraseLength);
                if (progress > 1.0f) progress = 1.0f;
                std::strncpy(status, "Listen to the rhythm line.", sizeof(status));
            }
        } else if (beatIndex < kPhraseLength) {
            const u64 target = phaseStart + phrase[beatIndex].targetMs;
            const u64 responseElapsed = now > phaseStart ? now - phaseStart : 0;
            progress = static_cast<float>(responseElapsed) /
                       static_cast<float>(kBeatMs * kPhraseLength);
            if (progress > 1.0f) progress = 1.0f;

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
                    playJudgement(lastJudgement);
                    judgedCurrentBeat = true;
                }
            }
            if (!judgedCurrentBeat && now > target + kGoodWindowMs) {
                std::strncpy(lastJudgement, "MISS", sizeof(lastJudgement));
                combo = 0;
                playJudgement(lastJudgement);
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
        drawRhythmStage(responsePhase, beatIndex < kPhraseLength ? beatIndex : kPhraseLength - 1,
                        progress, phrase, lastJudgement);
        presentFrame();
    }

    consoleClear();
    printf("\x1b[1;1H KIRBY DANCE PARTY");
    printf("\x1b[5;3HPHRASE COMPLETE!");
    printf("\x1b[8;3HFinal score: %d", score);
    printf("\x1b[11;3HThanks for dancing!");
    printf("\x1b[15;1HPress A to return to HOME.");
    printf("\x1b[20;1H[ A ] RETURN TO HOME");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_A) break;
        drawRhythmStage(true, kPhraseLength - 1, 1.0f, phrase, lastJudgement);
        presentFrame();
    }
}
} // namespace

int main(int argc, char* argv[]) {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, nullptr);
    initAudio();

    const Beat previewPhrase[kPhraseLength] = {
        {Direction::Left, 0},
        {Direction::Up, kBeatMs},
        {Direction::Right, kBeatMs * 2},
        {Direction::Down, kBeatMs * 3}
    };

    consoleClear();
    printf("KIRBY DANCE PARTY\n");
    printf("------------------------------\n");
    printf("A tiny rhythm prototype!\n\n");
    printf("Watch the call on the top screen,\n");
    printf("then repeat it with the D-Pad.\n\n");
    printf("Press A to start.\n");

    bool started = false;
    while (aptMainLoop() && !started) {
        hidScanInput();
        if (hidKeysDown() & KEY_A) started = true;
        if (hidKeysDown() & KEY_START) break;
        drawRhythmStage(false, 0, 0.0f, previewPhrase, "READY");
        presentFrame();
    }

    if (started) runPrototype();

    shutdownAudio();
    gfxExit();
    return 0;
}
