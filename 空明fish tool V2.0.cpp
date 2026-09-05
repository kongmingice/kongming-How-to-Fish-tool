#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cmath>
#include <unordered_map>
#include <string>
#include <vector>
#include <commctrl.h>          // 滑块控件支持

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "msimg32.lib")

// ================= 宏定义 =================
#define IDC_GET_ADDRESS       201
#define IDC_INIT_DRAW         202
#define IDC_DRAW_DYNAMIC      203
#define IDC_DRAW_FISH         204
#define IDC_DRAW_DROP         205
#define IDC_DEBUG_MODE        206
#define IDC_DRAW_NPC          207
#define IDC_DRAW_PLAYER       208
#define IDC_DRAW_ISLAND       209
#define IDC_DRAW_ALL          210
#define IDC_SPRINT_BOOST      211
#define IDC_HIGH_JUMP         212
#define IDC_INFINITE_GOLD     213
#define IDC_RESERVE1          214
#define IDC_RESERVE2          215
#define IDC_RESERVE3          216
#define IDC_BTN_CLOSE         217
#define IDC_AIMBOT            218
#define IDC_ROULETTE_FREEZE  219
#define IDC_ROULETTE_INSTANT 220
#define TITLE_HEIGHT 36
#define MAX_ENTITY_LIMIT 6000
#define TARGET_SELF         0xFF28FB7F
#define TARGET_PLAYER       0xFF28BB3F
#define TARGET_NPC          0xFFFFFFFF
#define TARGET_FISH_SEAGULL 0xF7380000
#define TARGET_ISLAND       0xFF2E9BBF
#define AIMBOT_RADIUS 150
#define AIMBOT_KEY VK_RBUTTON
#define DYNAMIC_HOLD_FRAMES 10
#define IDC_AIM_RADIUS    301
#define IDC_AIM_SMOOTH    302
#define IDC_AIM_PREDICT   303
#define IDC_AIM_RADIUS     301
#define IDC_AIM_SMOOTH     302
#define IDC_AIM_PREDICT    303
#define IDC_STATIC_RADIUS_LABEL  304
#define IDC_STATIC_RADIUS_VAL    305
#define IDC_STATIC_SMOOTH_LABEL  306
#define IDC_STATIC_SMOOTH_VAL    307
#define IDC_STATIC_PREDICT_LABEL 308
#define IDC_STATIC_PREDICT_VAL   309

// ================= 结构体 =================
struct Vector3 { float x, y, z; };

struct AppConfig {
    bool bGetAddress = false;
    bool bInitDraw = false;
    bool bDrawDynamic = false;
    bool bDrawFishSeagull = false;
    bool bDrawDropEquip = false;
    bool bDebugMode = false;
    bool bDrawNPC = false;
    bool bDrawPlayer = false;
    bool bDrawIsland = false;
    bool bDrawAll = false;
    bool bSprintBoost = false;
    bool bHighJump = false;
    bool bInfiniteGold = false;
    bool bAimbot = false;
    bool bReserve1 = false;
    bool bReserve2 = false;
    bool bReserve3 = false;
    bool bRouletteFreeze = false;
    bool bRouletteInstant = false;
};

// ================= 全局变量 =================
AppConfig g_config;
HANDLE g_hProcess = NULL;
uintptr_t g_entityArray = 0;
uintptr_t g_matrixAddr = 0;
uintptr_t g_sprintAddr = 0;
uintptr_t g_highJumpAddr = 0;
uintptr_t g_goldAddr = 0;
uintptr_t g_bulletAddr = 0;
float g_originalGold = 0.0f;
bool g_goldRecorded = false;
HWND g_hOverlay = NULL;
HWND g_hGameWnd = NULL;
bool g_running = true;
DWORD g_pid = 0;
int g_npcIndex = 0;                    // NPC传送索引
int g_playerIndex = 0;                 // 玩家传送索引
uintptr_t g_rouletteBase = 0;
uintptr_t g_rouletteAwardAddr = 0;   // 奖项地址
uintptr_t g_rouletteSpeedAddr = 0;   // 速度地址
uintptr_t g_rouletteInstantAddr = 0; // 结算地址
// ---- 预选目标（用于绘制射线） ----
uintptr_t g_preAimTarget = 0;
Vector3 g_preAimWorldPos;
bool g_hasPreAim = false;

// ---- 玩家传送锁定变量 ----
uintptr_t g_lockedTeleportTarget = 0;  // 玩家传送锁定目标
bool g_teleportActive = false;         // 玩家传送是否激活
// 不再需要 NPC 传送锁定变量
std::unordered_map<uintptr_t, Vector3> g_lastPos;
std::unordered_map<uintptr_t, int> g_dynamicFrames;

uintptr_t g_lockedTarget = 0;
uintptr_t g_lockedEntPtr = 0;
float g_lockedPrediction = 0.0f;
char g_lockedType[32] = "None";
int g_playerLockHold = 0;

// ---- 自瞄可调参数 ----
int g_aimRadius = 150;          // 自瞄半径
float g_aimSmooth = 0.8f;       // 平滑度
float g_aimPrediction = 0.5f;   // 预测速度倍数
// ================= 工具函数 =================
bool ReadRemote(HANDLE hProc, uintptr_t addr, void* buf, size_t size) {
    SIZE_T bytesRead;
    return ReadProcessMemory(hProc, reinterpret_cast<LPCVOID>(addr), buf, size, &bytesRead) != 0;
}

bool WriteRemote(HANDLE hProc, uintptr_t addr, void* buf, size_t size) {
    SIZE_T bytesWritten;
    return WriteProcessMemory(hProc, reinterpret_cast<LPVOID>(addr), buf, size, &bytesWritten) != 0;
}

DWORD GetProcessIdByName(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(PROCESSENTRY32W);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

uintptr_t GetModuleBase(DWORD pid, const wchar_t* moduleName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32W me32{};
    me32.dwSize = sizeof(MODULEENTRY32W);
    uintptr_t base = 0;
    if (Module32FirstW(hSnap, &me32)) {
        do {
            if (_wcsicmp(me32.szModule, moduleName) == 0) {
                base = reinterpret_cast<uintptr_t>(me32.modBaseAddr);
                break;
            }
        } while (Module32NextW(hSnap, &me32));
    }
    CloseHandle(hSnap);
    return base;
}

bool InitializeAddresses(DWORD pid) {
    uintptr_t dllBase = GetModuleBase(pid, L"UnityPlayer.dll");
    if (!dllBase) return false;
    uintptr_t current;

    // ---- 实体数组 ----
    current = dllBase + 0x0217A2A0;
    if (!ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) return false;
    current += 0x430;
    if (!ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) return false;
    g_entityArray = current;

    // ---- 视图矩阵 ----
    current = dllBase + 0x021F1E28;
    if (!ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) return false;
    const uintptr_t matrixOffsets[] = { 0x90, 0x10, 0x20, 0x20, 0x18, 0x2FC };
    const int offsetCount = sizeof(matrixOffsets) / sizeof(matrixOffsets[0]);
    for (int i = 0; i < offsetCount; i++) {
        current += matrixOffsets[i];
        if (i < offsetCount - 1) {
            if (!ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) return false;
        }
    }
    g_matrixAddr = current;

    uintptr_t monoBase = GetModuleBase(pid, L"mono-2.0-bdwgc.dll");
    if (monoBase) {
        // ---- 高跳和疾跑 ----
        current = monoBase + 0x00763250;
        if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
            current += 0x210;
            if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                current += 0x20;
                if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                    current += 0x540;
                    if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                        current += 0x138;
                        if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                            current += 0x0;
                            if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                                current += 0x100;
                                if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                                    g_highJumpAddr = current + 0x178;   // 跳跃高度
                                    g_sprintAddr = current + 0x15C;   // 疾跑速度
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- 无限金币 ----
        current = monoBase + 0x007390B8;
        if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
            current += 0x10;
            if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                g_goldAddr = current + 0xC48;
            }
        }

        // ---- 美国子弹 ----
        current = monoBase + 0x00A04430;
        if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
            current += 0x118;
            if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                current += 0x638;
                if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                    current += 0x80;
                    if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                        current += 0x0;
                        if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                            current += 0x110;
                            if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                                current += 0x110;
                                if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                                    current += 0x43C;
                                    g_bulletAddr = current;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- ★ 轮盘地址（基于实体数组） ----
    if (g_entityArray) {
        uintptr_t addr = g_entityArray;
        addr += 0x2D0;
        if (ReadRemote(g_hProcess, addr, &addr, sizeof(uintptr_t))) {
            addr += 0x10;
            if (ReadRemote(g_hProcess, addr, &addr, sizeof(uintptr_t))) {
                addr += 0x90;
                if (ReadRemote(g_hProcess, addr, &addr, sizeof(uintptr_t))) {
                    addr += 0x50;
                    if (ReadRemote(g_hProcess, addr, &addr, sizeof(uintptr_t))) {
                        addr += 0x10;
                        if (ReadRemote(g_hProcess, addr, &addr, sizeof(uintptr_t))) {
                            g_rouletteAwardAddr = addr + 0xF8;   // 奖项地址
                            g_rouletteSpeedAddr = addr + 0x114;  // 速度地址
                            g_rouletteInstantAddr = addr + 0x118; // 结算地址
                            g_rouletteBase = addr;
                        }
                    }
                }
            }
        }
    }
    return true;
}
// ================= 世界坐标转屏幕坐标 =================
bool WorldToScreen(const Vector3& world, float* viewProj, int scrW, int scrH, Vector3& screen) {
    float clipX = world.x * viewProj[0] + world.y * viewProj[4] + world.z * viewProj[8] + viewProj[12];
    float clipY = world.x * viewProj[1] + world.y * viewProj[5] + world.z * viewProj[9] + viewProj[13];
    float clipW = world.x * viewProj[3] + world.y * viewProj[7] + world.z * viewProj[11] + viewProj[15];
    if (clipW < 0.01f) return false;
    screen.x = (clipX / clipW * 0.5f + 0.5f) * static_cast<float>(scrW);
    screen.y = (1.0f - (clipY / clipW * 0.5f + 0.5f)) * static_cast<float>(scrH);
    return true;
}

// ================= 动态状态更新（带缓冲） =================
bool UpdateDynamicStatus(uintptr_t ent, const Vector3& currentPos) {
    auto it = g_dynamicFrames.find(ent);
    float speedSq = 0.0f;
    auto itPos = g_lastPos.find(ent);
    if (itPos != g_lastPos.end()) {
        float dx = currentPos.x - itPos->second.x;
        float dy = currentPos.y - itPos->second.y;
        float dz = currentPos.z - itPos->second.z;
        speedSq = dx * dx + dy * dy + dz * dz;
    }
    g_lastPos[ent] = currentPos;

    if (speedSq > 0.001f) {
        g_dynamicFrames[ent] = DYNAMIC_HOLD_FRAMES;
        return true;
    }
    else {
        if (it != g_dynamicFrames.end() && it->second > 0) {
            it->second--;
            if (it->second > 0) return true;
        }
        g_dynamicFrames[ent] = 0;
        return false;
    }
}

void DoAimbot(int screenWidth, int screenHeight) {
    // ---- 1. 目标选择（无论是否按右键，都更新预选目标） ----
    const bool aimNPC = g_config.bDrawNPC;
    const bool aimFish = g_config.bDrawFishSeagull;
    const bool aimPlayer = g_config.bDrawPlayer;
    const bool onlyDynamic = g_config.bDrawDynamic;
    if (!aimNPC && !aimFish && !aimPlayer) {
        g_lockedTarget = 0;
        g_lockedEntPtr = 0;
        g_preAimTarget = 0;
        g_hasPreAim = false;
        return;
    }

    float ViewProj[16];
    if (!ReadRemote(g_hProcess, g_matrixAddr, ViewProj, sizeof(ViewProj))) {
        g_preAimTarget = 0;
        g_hasPreAim = false;
        return;
    }

    // ---- 辅助函数：检查目标是否有效 ----
    auto IsTargetValid = [&](uintptr_t ent, Vector3& outPos) -> bool {
        if (ent == 0) return false;
        if (!ReadRemote(g_hProcess, ent + 0xA0, &outPos, sizeof(Vector3))) return false;
        Vector3 screenPos;
        if (!WorldToScreen(outPos, ViewProj, screenWidth, screenHeight, screenPos)) return false;
        if (screenPos.x < -200 || screenPos.x > screenWidth + 200 ||
            screenPos.y < -200 || screenPos.y > screenHeight + 200) return false;
        return true;
        };

    // ---- 目标选择循环 ----
    struct TargetCandidate {
        Vector3 worldPos;
        float   predictionTime;
        float   smooth;
        char    type[32];
        DWORD   val60;
        float   val9C;
        float   y;
        bool    usedPrediction;
        uintptr_t ent;
        uintptr_t entPtr;
    } best = {};
    best.predictionTime = 0.0f;
    best.smooth = 1.0f;
    strcpy_s(best.type, "None");
    float bestDistSq = FLT_MAX;

    const float FISH_EPSILON = 0.005f;
    const float fishValues[] = { 0.07f, 0.12f, 0.18f, 0.38f, 0.27f };
    const int fishCount = sizeof(fishValues) / sizeof(fishValues[0]);
    auto IsFishValue = [&](float val) -> bool {
        for (int j = 0; j < fishCount; ++j) {
            if (fabs(val - fishValues[j]) < FISH_EPSILON) return true;
        }
        return false;
        };

    for (int i = 0; i < MAX_ENTITY_LIMIT; ++i) {
        uintptr_t entPtr = 0;
        uintptr_t elemAddr = g_entityArray + i * sizeof(uintptr_t);
        if (!ReadRemote(g_hProcess, elemAddr, &entPtr, sizeof(uintptr_t))) continue;
        if (entPtr == 0) continue;

        DWORD val60 = 0;
        float val9C = 0.0f;
        ReadRemote(g_hProcess, entPtr + 0x60, &val60, sizeof(DWORD));
        ReadRemote(g_hProcess, entPtr + 0x9C, &val9C, sizeof(float));

        bool isTarget = false;
        if (aimNPC && val60 == TARGET_NPC) isTarget = true;
        else if (aimFish && val60 == TARGET_FISH_SEAGULL) isTarget = true;
        else if (aimPlayer && val60 == TARGET_PLAYER) isTarget = true;
        if (!isTarget) continue;

        uintptr_t ent = 0;
        if (!ReadRemote(g_hProcess, entPtr + 0x28, &ent, sizeof(uintptr_t))) continue;
        if (ent == 0) continue;

        Vector3 pos;
        if (!ReadRemote(g_hProcess, ent + 0xA0, &pos, sizeof(Vector3))) continue;

        const bool isFish = IsFishValue(val9C);
        if (isFish && pos.y > 10.0f) continue;

        if (val60 == TARGET_FISH_SEAGULL && onlyDynamic) {
            auto it = g_lastPos.find(ent);
            if (it != g_lastPos.end()) {
                const Vector3& last = it->second;
                float dx = pos.x - last.x;
                float dy = pos.y - last.y;
                float dz = pos.z - last.z;
                float speedSq = dx * dx + dy * dy + dz * dz;
                if (speedSq < 0.0001f) continue;
            }
        }

        float predictionTime = 0.0f;
        char typeStr[32] = "Unknown";
        if (val60 == TARGET_FISH_SEAGULL) {
            if (fabs(val9C - 0.25f) < FISH_EPSILON) {
                predictionTime = 0.3f;
                strcpy_s(typeStr, "Seagull");
            }
            else if (isFish) {
                predictionTime = 0.05f;
                strcpy_s(typeStr, "Fish");
            }
            else {
                predictionTime = 0.1f;
                sprintf_s(typeStr, "Unknown(%.2f)", val9C);
            }
        }
        else if (val60 == TARGET_NPC) {
            predictionTime = 0.0f;
            strcpy_s(typeStr, "NPC");
        }
        else if (val60 == TARGET_PLAYER) {
            predictionTime = 0.06f;
            strcpy_s(typeStr, "Player");
        }

        Vector3 predictedPos = pos;
        bool hasPrediction = false;
        if (predictionTime > 0.0f) {
            auto it = g_lastPos.find(ent);
            if (it != g_lastPos.end()) {
                const Vector3& lastPos = it->second;
                Vector3 velocity = { pos.x - lastPos.x, pos.y - lastPos.y, pos.z - lastPos.z };
                const float fps = 120.0f;
                // 使用 g_aimPrediction 作为速度倍数
                const float timeScale = fps * predictionTime * g_aimPrediction;
                predictedPos.x += velocity.x * timeScale;
                predictedPos.y += velocity.y * timeScale;
                predictedPos.z += velocity.z * timeScale;
                hasPrediction = true;
            }
        }

        Vector3 screenPos;
        if (hasPrediction) {
            if (!WorldToScreen(predictedPos, ViewProj, screenWidth, screenHeight, screenPos)) {
                if (!WorldToScreen(pos, ViewProj, screenWidth, screenHeight, screenPos)) continue;
                predictedPos = pos;
            }
        }
        else {
            if (!WorldToScreen(pos, ViewProj, screenWidth, screenHeight, screenPos)) continue;
            predictedPos = pos;
        }

        if (screenPos.x < 0 || screenPos.x > screenWidth ||
            screenPos.y < 0 || screenPos.y > screenHeight) continue;

        float dx = screenPos.x - screenWidth / 2.0f;
        float dy = screenPos.y - screenHeight / 2.0f;
        float distSq = dx * dx + dy * dy;
        // 使用 g_aimRadius
        if (distSq > g_aimRadius * g_aimRadius) continue;

        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            best.worldPos = predictedPos;
            best.predictionTime = predictionTime;
            best.smooth = 1.0f;
            strcpy_s(best.type, typeStr);
            best.val60 = val60;
            best.val9C = val9C;
            best.y = pos.y;
            best.usedPrediction = hasPrediction;
            best.ent = ent;
            best.entPtr = entPtr;
        }
    }

    // ---- 更新预选目标（始终更新） ----
    if (bestDistSq < FLT_MAX) {
        g_preAimTarget = best.ent;
        g_preAimWorldPos = best.worldPos;
        g_hasPreAim = true;
    }
    else {
        g_hasPreAim = false;
        g_preAimTarget = 0;
    }

    // ---- 2. 右键检查：如果未按右键，清除锁定并返回（不移动鼠标） ----
    if (!(GetAsyncKeyState(AIMBOT_KEY) & 0x8000)) {
        g_lockedTarget = 0;
        g_lockedEntPtr = 0;
        return;
    }

    // ---- 以下代码仅当按下右键时执行 ----
    if (!g_config.bAimbot) {
        g_lockedTarget = 0;
        g_lockedEntPtr = 0;
        return;
    }

    // ---- 3. 如果已有锁定目标，持续追踪 ----
    if (g_lockedTarget != 0 && g_lockedEntPtr != 0) {
        Vector3 pos;
        if (IsTargetValid(g_lockedTarget, pos)) {
            DWORD val60 = 0;
            float val9C = 0.0f;
            if (!ReadRemote(g_hProcess, g_lockedEntPtr + 0x60, &val60, sizeof(DWORD))) { g_lockedTarget = 0; g_lockedEntPtr = 0; return; }
            if (!ReadRemote(g_hProcess, g_lockedEntPtr + 0x9C, &val9C, sizeof(float))) { g_lockedTarget = 0; g_lockedEntPtr = 0; return; }

            bool isFish = IsFishValue(val9C);
            bool isSeagull = (val60 == TARGET_FISH_SEAGULL && !isFish && fabs(val9C - 0.25f) < FISH_EPSILON);

            float predictionTime = 0.0f;
            float speedFactor = 1.0f;
            if (val60 == TARGET_FISH_SEAGULL) {
                if (isSeagull) {
                    predictionTime = 0.15f;
                    speedFactor = 0.9f;
                }
                else if (isFish) {
                    predictionTime = 0.035f;
                    speedFactor = 0.9f;
                }
                else {
                    predictionTime = 0.035f;
                    speedFactor = 0.9f;
                }
            }
            else if (val60 == TARGET_NPC) {
                predictionTime = 0.0f;
                speedFactor = 1.0f;
            }
            else if (val60 == TARGET_PLAYER) {
                predictionTime = 0.02f;
                speedFactor = 0.9f;
            }

            Vector3 predictedPos = pos;
            if (predictionTime > 0.0f) {
                auto it = g_lastPos.find(g_lockedTarget);
                if (it != g_lastPos.end()) {
                    const Vector3& lastPos = it->second;
                    Vector3 velocity = { pos.x - lastPos.x, pos.y - lastPos.y, pos.z - lastPos.z };
                    const float fps = 120.0f;
                    // 使用 g_aimPrediction
                    const float timeScale = fps * predictionTime * speedFactor * g_aimPrediction;
                    predictedPos.x += velocity.x * timeScale;
                    predictedPos.y += velocity.y * timeScale;
                    predictedPos.z += velocity.z * timeScale;
                }
            }
            g_lastPos[g_lockedTarget] = pos;

            Vector3 targetScreen;
            if (!WorldToScreen(predictedPos, ViewProj, screenWidth, screenHeight, targetScreen)) {
                if (!WorldToScreen(pos, ViewProj, screenWidth, screenHeight, targetScreen)) {
                    return;
                }
                predictedPos = pos;
            }

            if (val60 == TARGET_NPC || val60 == TARGET_PLAYER) {
                targetScreen.y -= 20.0f;
            }

            int deltaX = static_cast<int>(targetScreen.x - screenWidth / 2.0f);
            int deltaY = static_cast<int>(targetScreen.y - screenHeight / 2.0f);

            // 使用 g_aimSmooth
            const float SMOOTH_FACTOR = g_aimSmooth;
            int moveX = static_cast<int>(deltaX * SMOOTH_FACTOR);
            int moveY = static_cast<int>(deltaY * SMOOTH_FACTOR);
            if (moveX != 0 || moveY != 0)
                mouse_event(MOUSEEVENTF_MOVE, moveX, moveY, 0, 0);

            g_preAimTarget = g_lockedTarget;
            g_preAimWorldPos = predictedPos;
            g_hasPreAim = true;

            if (g_config.bDebugMode) {
                HDC hdc = GetDC(g_hOverlay);
                if (hdc) {
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(0, 255, 255));
                    char buf[256];
                    sprintf_s(buf, "Locked: %s | pred=%.2f | delta=(%d,%d)",
                        isSeagull ? "Seagull" : (isFish ? "Fish" : "Other"), predictionTime, deltaX, deltaY);
                    TextOutA(hdc, 10, 10, buf, (int)strlen(buf));
                    ReleaseDC(g_hOverlay, hdc);
                }
            }
            return;
        }
        else {
            g_lockedTarget = 0;
            g_lockedEntPtr = 0;
        }
    }

    // ---- 4. 无锁定，锁定最佳目标 ----
    if (bestDistSq < FLT_MAX) {
        g_lockedTarget = best.ent;
        g_lockedEntPtr = best.entPtr;

        Vector3 targetScreen;
        if (WorldToScreen(best.worldPos, ViewProj, screenWidth, screenHeight, targetScreen)) {
            if (best.val60 == TARGET_NPC || best.val60 == TARGET_PLAYER)
                targetScreen.y -= 20.0f;
            int deltaX = static_cast<int>(targetScreen.x - screenWidth / 2.0f);
            int deltaY = static_cast<int>(targetScreen.y - screenHeight / 2.0f);
            if (abs(deltaX) >= 5 || abs(deltaY) >= 5) {
                // ★ 使用 g_aimSmooth
                const float SMOOTH_FACTOR = g_aimSmooth;
                int moveX = static_cast<int>(deltaX * SMOOTH_FACTOR);
                int moveY = static_cast<int>(deltaY * SMOOTH_FACTOR);
                mouse_event(MOUSEEVENTF_MOVE, moveX, moveY, 0, 0);
            }
        }

        if (g_config.bDebugMode) {
            HDC hdc = GetDC(g_hOverlay);
            if (hdc) {
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(0, 255, 255));
                char buf[256];
                sprintf_s(buf, "Locked NEW: %s | pred=%.2f", best.type, best.predictionTime);
                TextOutA(hdc, 10, 10, buf, (int)strlen(buf));
                ReleaseDC(g_hOverlay, hdc);
            }
        }
    }
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rect;
        GetClientRect(hwnd, &rect);
        int w = rect.right, h = rect.bottom;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

        HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(memDC, &rect, blackBrush);
        DeleteObject(blackBrush);

        if (!g_config.bInitDraw || !g_entityArray || !g_matrixAddr) {
            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps);
            return 0;
        }

        float ViewProj[16];
        if (!ReadRemote(g_hProcess, g_matrixAddr, ViewProj, sizeof(ViewProj))) {
            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps);
            return 0;
        }

        std::vector<uintptr_t> entityPtrs(MAX_ENTITY_LIMIT);
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(g_hProcess, (LPCVOID)g_entityArray, entityPtrs.data(),
            MAX_ENTITY_LIMIT * sizeof(uintptr_t), &bytesRead)) {
            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps);
            return 0;
        }
        int validCount = static_cast<int>(bytesRead / sizeof(uintptr_t));

        Vector3 playerPos = { 0,0,0 };
        bool hasPlayer = false;
        for (int i = 0; i < validCount; ++i) {
            uintptr_t entPtr = entityPtrs[i];
            if (!entPtr) continue;
            DWORD val60 = 0;
            if (!ReadRemote(g_hProcess, entPtr + 0x60, &val60, sizeof(DWORD))) continue;
            if (val60 == TARGET_SELF) {
                uintptr_t ent = 0;
                if (ReadRemote(g_hProcess, entPtr + 0x28, &ent, sizeof(uintptr_t)) && ent) {
                    if (ReadRemote(g_hProcess, ent + 0xA0, &playerPos, sizeof(Vector3))) {
                        hasPlayer = true;
                    }
                }
                break;
            }
        }

        HFONT hFont = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
        HGDIOBJ oldFont = SelectObject(memDC, hFont);

        HPEN penYellow = CreatePen(PS_SOLID, 1, RGB(255, 255, 0));
        HPEN penGreen = CreatePen(PS_SOLID, 1, RGB(0, 255, 0));
        HPEN penRed = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
        HPEN penBlue = CreatePen(PS_SOLID, 1, RGB(0, 0, 255));
        HPEN penWhite = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        HBRUSH nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
        HBRUSH redBrush = CreateSolidBrush(RGB(255, 0, 0));

        int centerX = w / 2;
        int centerY = h / 2;

        const float FISH_EPSILON = 0.005f;
        const float fishValues[] = { 0.07f, 0.12f, 0.18f, 0.38f, 0.27f };
        const int fishCount = sizeof(fishValues) / sizeof(fishValues[0]);

        auto IsFishValue = [&](float val) -> bool {
            for (int j = 0; j < fishCount; ++j) {
                if (fabs(val - fishValues[j]) < FISH_EPSILON) return true;
            }
            return false;
            };

        for (int i = 0; i < validCount; ++i) {
            uintptr_t entPtr = entityPtrs[i];
            if (!entPtr) continue;

            DWORD val60 = 0;
            float val9C = 0.0f;
            if (!ReadRemote(g_hProcess, entPtr + 0x60, &val60, sizeof(DWORD))) continue;
            if (!ReadRemote(g_hProcess, entPtr + 0x9C, &val9C, sizeof(float))) continue;

            bool isNPC = (val60 == TARGET_NPC && g_config.bDrawNPC && fabs(val9C - 0.17f) < 0.02f);
            bool isPlayer = (val60 == TARGET_PLAYER && g_config.bDrawPlayer);
            bool isIsland = (val60 == TARGET_ISLAND && g_config.bDrawIsland &&
                (fabs(val9C - 240.0f) < 0.01f || fabs(val9C - 100.0f) < 0.01f));
            bool isDropEquip = false;
            if (val60 == TARGET_FISH_SEAGULL && g_config.bDrawDropEquip) {
                if (fabs(val9C - 0.11f) < FISH_EPSILON || fabs(val9C - 0.02f) < FISH_EPSILON ||
                    fabs(val9C - 0.01f) < FISH_EPSILON || fabs(val9C - 0.15f) < FISH_EPSILON ||
                    fabs(val9C - 0.06f) < FISH_EPSILON || fabs(val9C - 0.03f) < FISH_EPSILON) isDropEquip = true;
            }

            bool isFishSeagull = (val60 == TARGET_FISH_SEAGULL);
            bool isFish = IsFishValue(val9C);
            bool isSeagull = (isFishSeagull && !isFish && fabs(val9C - 0.25f) < FISH_EPSILON);

            bool isDynamic = false;
            if (isFishSeagull) {
                uintptr_t entTmp = 0;
                if (ReadRemote(g_hProcess, entPtr + 0x28, &entTmp, sizeof(uintptr_t)) && entTmp) {
                    Vector3 posTmp;
                    if (ReadRemote(g_hProcess, entTmp + 0xA0, &posTmp, sizeof(Vector3))) {
                        isDynamic = UpdateDynamicStatus(entTmp, posTmp);
                    }
                }
            }

            bool shouldDraw = false;
            if (g_config.bDrawAll) {
                shouldDraw = true;
            }
            else {
                if (isNPC || isPlayer || isDropEquip || isIsland) shouldDraw = true;
                if (g_config.bDrawFishSeagull && isFishSeagull) {
                    if (isSeagull || isFish) {
                        if (g_config.bDrawDynamic) {
                            if (isDynamic) shouldDraw = true;
                        }
                        else {
                            shouldDraw = true;
                        }
                    }
                }
            }
            if (isIsland) shouldDraw = true;

            if (!shouldDraw) continue;

            uintptr_t ent = 0;
            if (!ReadRemote(g_hProcess, entPtr + 0x28, &ent, sizeof(uintptr_t))) continue;
            if (ent == 0) continue;
            Vector3 pos;
            if (!ReadRemote(g_hProcess, ent + 0xA0, &pos, sizeof(Vector3))) continue;
            if (fabs(pos.x) > 10000 || fabs(pos.y) > 10000 || fabs(pos.z) > 10000) continue;

            // ---- 距离过滤（岛屿和海鸥不受 30/50 限制） ----
            if (hasPlayer && val60 != TARGET_ISLAND) {
                float dx = pos.x - playerPos.x;
                float dy = pos.y - playerPos.y;
                float dz = pos.z - playerPos.z;
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                if (dist > 300.0f) continue;
                if (!isSeagull) {
                    float limit = g_config.bDrawAll ? 30.0f : 50.0f;
                    if (dist > limit) continue;
                }
            }

            if (isFish && pos.y > 10.0f) continue;

            Vector3 screen;
            if (!WorldToScreen(pos, ViewProj, w, h, screen)) continue;
            if (screen.x < 0 || screen.x > w || screen.y < 0 || screen.y > h) continue;

            // ========== 岛屿绘制为红点 ==========
            if (isIsland) {
                HPEN oldPen = (HPEN)SelectObject(memDC, penRed);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, redBrush);
                Rectangle(memDC, (int)screen.x - 2, (int)screen.y - 2,
                    (int)screen.x + 2, (int)screen.y + 2);
                SelectObject(memDC, oldPen);
                SelectObject(memDC, oldBrush);
                continue;
            }
            // ========================================

            // ---- 距离过滤（岛屿完全不受限制） ----
            if (isIsland) {
                // 已 continue
            }
            else if (hasPlayer) {
                float dx = pos.x - playerPos.x;
                float dy = pos.y - playerPos.y;
                float dz = pos.z - playerPos.z;
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                if (dist > 300.0f) continue;
                if (!isSeagull) {
                    float limit = g_config.bDrawAll ? 30.0f : 50.0f;
                    if (dist > limit) continue;
                }
            }

            bool isSelf = (val60 == TARGET_SELF);
            float scale = 1.0f;
            if (!isSelf && hasPlayer) {
                float dx = pos.x - playerPos.x;
                float dy = pos.y - playerPos.y;
                float dz = pos.z - playerPos.z;
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                if (dist > 0.1f) {
                    scale = 1.0f / (1.0f + dist * 0.12f);
                    if (scale < 0.15f) scale = 0.15f;
                    if (scale > 1.5f) scale = 1.5f;
                }
            }
            int baseHalf = static_cast<int>(30 * scale);
            if (baseHalf < 5) baseHalf = 5;

            HPEN pen = penWhite;
            int halfW = baseHalf, halfH = baseHalf;

            if (isSelf) {
                pen = penBlue;
                halfW = 30; halfH = 30;
            }
            else if (isDropEquip) {
                pen = penBlue;
                halfW = baseHalf; halfH = baseHalf;
            }
            else if (isNPC) {
                pen = penGreen;
                halfW = static_cast<int>(baseHalf * 1.5f);
                halfH = static_cast<int>(baseHalf * 3.0f);
            }
            else if (isPlayer) {
                pen = penRed;
                halfW = static_cast<int>(baseHalf * 1.5f);
                halfH = static_cast<int>(baseHalf * 3.0f);
            }
            else if (isFishSeagull) {
                if (g_config.bDrawDynamic && isDynamic)
                    pen = penYellow;
                else
                    pen = penWhite;
                halfW = baseHalf;
                halfH = baseHalf;
            }

            HPEN oldPen = (HPEN)SelectObject(memDC, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, nullBrush);
            Rectangle(memDC, (int)screen.x - halfW, (int)screen.y - halfH,
                (int)screen.x + halfW, (int)screen.y + halfH);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBrush);

            if (g_config.bDebugMode) {
                char idText[16], valText[16];
                sprintf_s(idText, sizeof(idText), "%08X", val60);
                sprintf_s(valText, sizeof(valText), "%.2f", val9C);
                SetBkMode(memDC, TRANSPARENT);
                SetTextColor(memDC, RGB(0, 255, 255));
                TextOutA(memDC, (int)screen.x + 20, (int)screen.y + 10, idText, (int)strlen(idText));
                TextOutA(memDC, (int)screen.x + 20, (int)screen.y + 28, valText, (int)strlen(valText));
            }
        }

        // ---- 射线 ----
        if (g_config.bAimbot) {
            uintptr_t target = 0;
            Vector3 pos;
            bool hasTarget = false;
            if (g_lockedTarget != 0) {
                target = g_lockedTarget;
                if (ReadRemote(g_hProcess, target + 0xA0, &pos, sizeof(Vector3))) {
                    hasTarget = true;
                }
            }
            else if (g_hasPreAim && g_preAimTarget != 0) {
                target = g_preAimTarget;
                pos = g_preAimWorldPos;
                hasTarget = true;
            }
            if (hasTarget) {
                Vector3 screen;
                if (WorldToScreen(pos, ViewProj, w, h, screen)) {
                    float dx = screen.x - centerX;
                    float dy = screen.y - centerY;
                    if (dx * dx + dy * dy <= g_aimRadius * g_aimRadius) {  // ★ 使用 g_aimRadius
                        HPEN penLine = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                        HPEN oldPen = (HPEN)SelectObject(memDC, penLine);
                        MoveToEx(memDC, centerX, centerY, NULL);
                        LineTo(memDC, (int)screen.x, (int)screen.y);
                        SelectObject(memDC, oldPen);
                        DeleteObject(penLine);
                    }
                }
            }
        }

        // ---- 自瞄圈和准心 ----
        if (g_config.bAimbot) {
            HPEN penCircle = CreatePen(PS_SOLID, 1, RGB(0, 255, 0));
            HPEN oldPen = (HPEN)SelectObject(memDC, penCircle);
            HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, nullBrush);
            Ellipse(memDC, centerX - g_aimRadius, centerY - g_aimRadius,   // 使用 g_aimRadius
                centerX + g_aimRadius, centerY + g_aimRadius);
            SelectObject(memDC, oldPen);
            SelectObject(memDC, oldBrush);
            DeleteObject(penCircle);

            HPEN penCross = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
            SelectObject(memDC, penCross);
            MoveToEx(memDC, centerX - 10, centerY, NULL);
            LineTo(memDC, centerX + 10, centerY);
            MoveToEx(memDC, centerX, centerY - 10, NULL);
            LineTo(memDC, centerX, centerY + 10);
            DeleteObject(penCross);
        }

        SelectObject(memDC, oldFont);
        DeleteObject(hFont);
        DeleteObject(penYellow);
        DeleteObject(penGreen);
        DeleteObject(penRed);
        DeleteObject(penBlue);
        DeleteObject(penWhite);
        DeleteObject(redBrush);

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
LRESULT CALLBACK UIWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool bExpanded = true;
    static int windowWidth = 450;
    static int expandedHeight = 460;
    static int collapsedHeight = TITLE_HEIGHT;
    static HFONT hTitleFont = NULL;
    static HFONT hCtrlFont = NULL;
    static HWND hCloseBtn = NULL;

    switch (msg) {
    case WM_CREATE: {
        RECT rc;
        GetClientRect(hWnd, &rc);
        HRGN hRgn = CreateRoundRectRgn(0, 0, rc.right, rc.bottom, 12, 12);
        SetWindowRgn(hWnd, hRgn, TRUE);

        hTitleFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        hCtrlFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        int leftX = 15, rowY = 50, rowHeight = 28, colWidth = 185;
        // 左列（9个）
        CreateWindowW(L"BUTTON", L"获取地址", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            leftX, rowY, colWidth, rowHeight, hWnd, (HMENU)IDC_GET_ADDRESS, GetModuleHandle(NULL), NULL);
        rowY += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"初始化绘制", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            leftX, rowY, colWidth, rowHeight, hWnd, (HMENU)IDC_INIT_DRAW, GetModuleHandle(NULL), NULL);
        rowY += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"过滤动态物体", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            leftX, rowY, colWidth, rowHeight, hWnd, (HMENU)IDC_DRAW_DYNAMIC, GetModuleHandle(NULL), NULL);
        rowY += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"鱼类海鸥", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            leftX, rowY, colWidth, rowHeight, hWnd, (HMENU)IDC_DRAW_FISH, GetModuleHandle(NULL), NULL);
        rowY += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"掉落装备", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            leftX, rowY, colWidth, rowHeight, hWnd, (HMENU)IDC_DRAW_DROP, GetModuleHandle(NULL), NULL);
        rowY += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"调试模式", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            leftX, rowY, colWidth, rowHeight, hWnd, (HMENU)IDC_DEBUG_MODE, GetModuleHandle(NULL), NULL);
        rowY += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"无限金币", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            leftX, rowY, colWidth, rowHeight, hWnd, (HMENU)IDC_INFINITE_GOLD, GetModuleHandle(NULL), NULL);
        rowY += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"冻结轮盘奖项", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            leftX, rowY, colWidth, rowHeight, hWnd, (HMENU)IDC_ROULETTE_FREEZE, GetModuleHandle(NULL), NULL);
        rowY += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"轮盘立即结算", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            leftX, rowY, colWidth, rowHeight, hWnd, (HMENU)IDC_ROULETTE_INSTANT, GetModuleHandle(NULL), NULL);

        // 右列（9个）
        int rightX = leftX + colWidth + 20, y = 50;
        CreateWindowW(L"BUTTON", L"NPC", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            rightX, y, colWidth, rowHeight, hWnd, (HMENU)IDC_DRAW_NPC, GetModuleHandle(NULL), NULL);
        y += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"玩家", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            rightX, y, colWidth, rowHeight, hWnd, (HMENU)IDC_DRAW_PLAYER, GetModuleHandle(NULL), NULL);
        y += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"岛屿", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            rightX, y, colWidth, rowHeight, hWnd, (HMENU)IDC_DRAW_ISLAND, GetModuleHandle(NULL), NULL);
        y += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"绘制全部", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            rightX, y, colWidth, rowHeight, hWnd, (HMENU)IDC_DRAW_ALL, GetModuleHandle(NULL), NULL);
        y += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"自瞄(右键)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            rightX, y, colWidth, rowHeight, hWnd, (HMENU)IDC_AIMBOT, GetModuleHandle(NULL), NULL);
        y += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"高跳加速", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            rightX, y, colWidth, rowHeight, hWnd, (HMENU)IDC_SPRINT_BOOST, GetModuleHandle(NULL), NULL);
        y += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"美国子弹", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            rightX, y, colWidth, rowHeight, hWnd, (HMENU)IDC_RESERVE1, GetModuleHandle(NULL), NULL);
        y += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"玩家传送(↑↓)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            rightX, y, colWidth, rowHeight, hWnd, (HMENU)IDC_RESERVE2, GetModuleHandle(NULL), NULL);
        y += rowHeight + 4;
        CreateWindowW(L"BUTTON", L"NPC传送(←→)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            rightX, y, colWidth, rowHeight, hWnd, (HMENU)IDC_RESERVE3, GetModuleHandle(NULL), NULL);

        // 关闭按钮
        hCloseBtn = CreateWindowW(L"BUTTON", L"✕", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_FLAT,
            windowWidth - 40, 4, 28, 28,
            hWnd, (HMENU)IDC_BTN_CLOSE, GetModuleHandle(NULL), NULL);
        SendMessage(hCloseBtn, WM_SETFONT, (WPARAM)hTitleFont, TRUE);

        // ---- 滑块区域（起始 y=340） ----
        int sliderY = 340;
        // 1. 自瞄半径 (范围 1~400)
        CreateWindowW(L"STATIC", L"自瞄半径", WS_CHILD | WS_VISIBLE,
            15, sliderY, 80, 20, hWnd, (HMENU)111, GetModuleHandle(NULL), NULL);
        HWND hRad = CreateWindowW(TRACKBAR_CLASS, NULL, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            100, sliderY - 2, 150, 25, hWnd, (HMENU)IDC_AIM_RADIUS, GetModuleHandle(NULL), NULL);
        SendMessage(hRad, TBM_SETRANGE, TRUE, MAKELONG(1, 400));
        SendMessage(hRad, TBM_SETPOS, TRUE, g_aimRadius);
        CreateWindowW(L"STATIC", L"150", WS_CHILD | WS_VISIBLE,
            255, sliderY, 40, 20, hWnd, (HMENU)112, GetModuleHandle(NULL), NULL);

        sliderY += 30;
        // 2. 平滑度 (范围 0.10~1.00，步长0.01)
        CreateWindowW(L"STATIC", L"平滑度", WS_CHILD | WS_VISIBLE,
            15, sliderY, 80, 20, hWnd, (HMENU)113, GetModuleHandle(NULL), NULL);
        HWND hSmooth = CreateWindowW(TRACKBAR_CLASS, NULL, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            100, sliderY - 2, 150, 25, hWnd, (HMENU)IDC_AIM_SMOOTH, GetModuleHandle(NULL), NULL);
        SendMessage(hSmooth, TBM_SETRANGE, TRUE, MAKELONG(10, 100));
        SendMessage(hSmooth, TBM_SETPOS, TRUE, (int)(g_aimSmooth * 100));
        CreateWindowW(L"STATIC", L"0.60", WS_CHILD | WS_VISIBLE,
            255, sliderY, 40, 20, hWnd, (HMENU)114, GetModuleHandle(NULL), NULL);

        sliderY += 30;
        // 3. 预测速度 (范围 0.1~3.0，步长0.1)
        CreateWindowW(L"STATIC", L"预测速度", WS_CHILD | WS_VISIBLE,
            15, sliderY, 80, 20, hWnd, (HMENU)115, GetModuleHandle(NULL), NULL);
        HWND hPred = CreateWindowW(TRACKBAR_CLASS, NULL, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
            100, sliderY - 2, 150, 25, hWnd, (HMENU)IDC_AIM_PREDICT, GetModuleHandle(NULL), NULL);
        SendMessage(hPred, TBM_SETRANGE, TRUE, MAKELONG(10, 300));
        SendMessage(hPred, TBM_SETPOS, TRUE, (int)(g_aimPrediction * 100));
        CreateWindowW(L"STATIC", L"1.00", WS_CHILD | WS_VISIBLE,
            255, sliderY, 40, 20, hWnd, (HMENU)116, GetModuleHandle(NULL), NULL);

        // 给所有子控件设置字体
        for (HWND hChild = GetWindow(hWnd, GW_CHILD); hChild; hChild = GetWindow(hChild, GW_HWNDNEXT)) {
            SendMessage(hChild, WM_SETFONT, (WPARAM)hCtrlFont, TRUE);
        }

        bExpanded = true;
        SetWindowPos(hWnd, NULL, 0, 0, windowWidth, expandedHeight, SWP_NOMOVE | SWP_NOZORDER);
        return 0;
    }

    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        HWND hBtn = (HWND)lParam;
        if (hBtn == hCloseBtn) {
            SetBkColor(hdc, RGB(43, 122, 236));
            SetTextColor(hdc, RGB(255, 255, 255));
            static HBRUSH hBrushClose = CreateSolidBrush(RGB(43, 122, 236));
            return (LRESULT)hBrushClose;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);

        HBRUSH bgBrush = CreateSolidBrush(RGB(245, 248, 255));
        FillRect(hdc, &rc, bgBrush);
        DeleteObject(bgBrush);

        RECT titleRc = { 0, 0, rc.right, TITLE_HEIGHT };
        HBRUSH titleBrush = CreateSolidBrush(RGB(43, 122, 236));
        FillRect(hdc, &titleRc, titleBrush);
        DeleteObject(titleBrush);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        HFONT hOldFont = (HFONT)SelectObject(hdc, hTitleFont);

        const wchar_t* arrow = bExpanded ? L"▾" : L"▸";
        RECT arrowRc = { 10, 2, 40, TITLE_HEIGHT - 2 };
        DrawTextW(hdc, arrow, -1, &arrowRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        RECT textRc = { 45, 2, rc.right - 50, TITLE_HEIGHT - 2 };
        DrawTextW(hdc, L"渔力全开 - How to fish - 空明 - V2.0", -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SelectObject(hdc, hOldFont);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (pt.y < TITLE_HEIGHT) {
            RECT btnRc;
            GetWindowRect(hCloseBtn, &btnRc);
            MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&btnRc, 2);
            if (!PtInRect(&btnRc, pt)) {
                bExpanded = !bExpanded;
                int newHeight = bExpanded ? expandedHeight : collapsedHeight;
                SetWindowPos(hWnd, NULL, 0, 0, windowWidth, newHeight, SWP_NOMOVE | SWP_NOZORDER);
                for (HWND hChild = GetWindow(hWnd, GW_CHILD); hChild; hChild = GetWindow(hChild, GW_HWNDNEXT)) {
                    if (hChild != hCloseBtn) {
                        ShowWindow(hChild, bExpanded ? SW_SHOW : SW_HIDE);
                    }
                }
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }
        }
        break;
    }

    case WM_NCHITTEST: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);
        if (pt.y < TITLE_HEIGHT) {
            RECT btnRc;
            GetWindowRect(hCloseBtn, &btnRc);
            MapWindowPoints(HWND_DESKTOP, hWnd, (LPPOINT)&btnRc, 2);
            if (!PtInRect(&btnRc, pt)) {
                return HTCAPTION;
            }
        }
        break;
    }

                     // ---- 处理滑块拖动 ----
    case WM_HSCROLL: {
        HWND hCtrl = (HWND)lParam;
        int id = GetWindowLongPtr(hCtrl, GWLP_ID);
        int pos = (int)SendMessage(hCtrl, TBM_GETPOS, 0, 0);
        if (id == IDC_AIM_RADIUS) {
            g_aimRadius = pos;
            HWND hStatic = GetDlgItem(hWnd, 112);
            if (hStatic) {
                char buf[16];
                sprintf_s(buf, "%d", pos);
                SetWindowTextA(hStatic, buf);
            }
        }
        else if (id == IDC_AIM_SMOOTH) {
            g_aimSmooth = pos / 100.0f;
            HWND hStatic = GetDlgItem(hWnd, 114);
            if (hStatic) {
                char buf[16];
                sprintf_s(buf, "%.2f", g_aimSmooth);
                SetWindowTextA(hStatic, buf);
            }
        }
        else if (id == IDC_AIM_PREDICT) {
            g_aimPrediction = pos / 100.0f;
            HWND hStatic = GetDlgItem(hWnd, 116);
            if (hStatic) {
                char buf[16];
                sprintf_s(buf, "%.2f", g_aimPrediction);
                SetWindowTextA(hStatic, buf);
            }
        }
        break;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        if (id == IDC_BTN_CLOSE) {
            DestroyWindow(hWnd);
            PostQuitMessage(0);
            return 0;
        }
        if (id == IDC_GET_ADDRESS) {
            g_config.bGetAddress = (IsDlgButtonChecked(hWnd, IDC_GET_ADDRESS) == BST_CHECKED);
            if (g_config.bGetAddress) {
                DWORD pid = 0;
                if (g_hGameWnd) {
                    GetWindowThreadProcessId(g_hGameWnd, &pid);
                }
                if (!pid) {
                    pid = GetProcessIdByName(L"How to Fish.exe");
                }
                if (!pid) {
                    MessageBoxA(NULL, "找不到游戏进程，请确认游戏已启动并进入主界面", "错误", MB_OK);
                    CheckDlgButton(hWnd, IDC_GET_ADDRESS, BST_UNCHECKED);
                    g_config.bGetAddress = false;
                    InvalidateRect(g_hOverlay, NULL, FALSE);
                    return 0;
                }
                g_pid = pid;
                if (g_hProcess) {
                    CloseHandle(g_hProcess);
                    g_hProcess = NULL;
                }
                g_hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
                if (!g_hProcess) {
                    MessageBoxA(NULL, "打开进程失败，请以管理员身份运行", "错误", MB_OK);
                    CheckDlgButton(hWnd, IDC_GET_ADDRESS, BST_UNCHECKED);
                    g_config.bGetAddress = false;
                    InvalidateRect(g_hOverlay, NULL, FALSE);
                    return 0;
                }
                if (!InitializeAddresses(pid)) {
                    MessageBoxA(NULL, "初始化地址失败，请检查游戏版本", "错误", MB_OK);
                    CheckDlgButton(hWnd, IDC_GET_ADDRESS, BST_UNCHECKED);
                    g_config.bGetAddress = false;
                    InvalidateRect(g_hOverlay, NULL, FALSE);
                    return 0;
                }
            }
            else {
                if (g_hProcess) {
                    CloseHandle(g_hProcess);
                    g_hProcess = NULL;
                }
                g_pid = 0;
                g_entityArray = 0;
                g_matrixAddr = 0;
                g_sprintAddr = 0;
                g_highJumpAddr = 0;
                g_goldAddr = 0;
                g_bulletAddr = 0;
                g_rouletteBase = 0;
                g_rouletteAwardAddr = 0;
                g_rouletteSpeedAddr = 0;
                g_rouletteInstantAddr = 0;
            }
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_INIT_DRAW) {
            g_config.bInitDraw = (IsDlgButtonChecked(hWnd, IDC_INIT_DRAW) == BST_CHECKED);
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_DRAW_DYNAMIC) {
            g_config.bDrawDynamic = (IsDlgButtonChecked(hWnd, IDC_DRAW_DYNAMIC) == BST_CHECKED);
            if (!g_config.bDrawDynamic) g_lastPos.clear();
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_DRAW_FISH) {
            g_config.bDrawFishSeagull = (IsDlgButtonChecked(hWnd, IDC_DRAW_FISH) == BST_CHECKED);
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_DRAW_DROP) {
            g_config.bDrawDropEquip = (IsDlgButtonChecked(hWnd, IDC_DRAW_DROP) == BST_CHECKED);
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_DEBUG_MODE) {
            g_config.bDebugMode = (IsDlgButtonChecked(hWnd, IDC_DEBUG_MODE) == BST_CHECKED);
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_DRAW_NPC) {
            g_config.bDrawNPC = (IsDlgButtonChecked(hWnd, IDC_DRAW_NPC) == BST_CHECKED);
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_DRAW_PLAYER) {
            g_config.bDrawPlayer = (IsDlgButtonChecked(hWnd, IDC_DRAW_PLAYER) == BST_CHECKED);
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_DRAW_ISLAND) {
            g_config.bDrawIsland = (IsDlgButtonChecked(hWnd, IDC_DRAW_ISLAND) == BST_CHECKED);
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_DRAW_ALL) {
            g_config.bDrawAll = (IsDlgButtonChecked(hWnd, IDC_DRAW_ALL) == BST_CHECKED);
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_AIMBOT) {
            g_config.bAimbot = (IsDlgButtonChecked(hWnd, IDC_AIMBOT) == BST_CHECKED);
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_SPRINT_BOOST) {
            bool bEnabled = (IsDlgButtonChecked(hWnd, IDC_SPRINT_BOOST) == BST_CHECKED);
            g_config.bSprintBoost = bEnabled;
            g_config.bHighJump = bEnabled;
            if (g_sprintAddr && g_highJumpAddr) {
                float sprintVal = bEnabled ? 15.0f : 7.5f;
                float jumpVal = bEnabled ? 12.0f : 9.0f;
                WriteRemote(g_hProcess, g_sprintAddr, &sprintVal, sizeof(float));
                WriteRemote(g_hProcess, g_highJumpAddr, &jumpVal, sizeof(float));
            }
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_INFINITE_GOLD) {
            g_config.bInfiniteGold = (IsDlgButtonChecked(hWnd, IDC_INFINITE_GOLD) == BST_CHECKED);
            if (g_goldAddr) {
                if (g_config.bInfiniteGold) {
                    float current;
                    if (ReadRemote(g_hProcess, g_goldAddr, &current, sizeof(float))) {
                        g_originalGold = current;
                        g_goldRecorded = true;
                        float newGold = 99999.0f;
                        WriteRemote(g_hProcess, g_goldAddr, &newGold, sizeof(float));
                    }
                }
                else if (g_goldRecorded) {
                    WriteRemote(g_hProcess, g_goldAddr, &g_originalGold, sizeof(float));
                }
            }
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        // 美国子弹
        else if (id == IDC_RESERVE1) {
            // 重新计算美国子弹地址
            uintptr_t bulletAddr = 0;
            uintptr_t monoBase = GetModuleBase(g_pid, L"mono-2.0-bdwgc.dll");

            if (monoBase != 0) {
                uintptr_t current = monoBase + 0x00A04430;
                if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                    current += 0x118;
                    if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                        current += 0x638;
                        if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                            current += 0x80;
                            if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                                current += 0x0;
                                if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                                    current += 0x110;
                                    if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                                        current += 0x110;
                                        if (ReadRemote(g_hProcess, current, &current, sizeof(uintptr_t))) {
                                            current += 0x43C;
                                            bulletAddr = current;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (bulletAddr == 0) {
                MessageBoxA(NULL, "子弹地址获取失败，请确认已手持武器并重新获取地址", "错误", MB_OK);
                CheckDlgButton(hWnd, IDC_RESERVE1, BST_UNCHECKED);
                g_config.bReserve1 = false;
                InvalidateRect(g_hOverlay, NULL, FALSE);
                return 0;
            }

            int currentValue = 0;
            if (!ReadRemote(g_hProcess, bulletAddr, &currentValue, sizeof(int))) {
                MessageBoxA(NULL, "读取子弹数量失败", "错误", MB_OK);
                CheckDlgButton(hWnd, IDC_RESERVE1, BST_UNCHECKED);
                g_config.bReserve1 = false;
                InvalidateRect(g_hOverlay, NULL, FALSE);
                return 0;
            }

            if (currentValue != 1 && currentValue != 100) {
                char msg[256];
                sprintf_s(msg, "当前子弹值异常: %d，请手持武器重试", currentValue);
                MessageBoxA(NULL, msg, "提示", MB_OK);
                CheckDlgButton(hWnd, IDC_RESERVE1, BST_UNCHECKED);
                g_config.bReserve1 = false;
                InvalidateRect(g_hOverlay, NULL, FALSE);
                return 0;
            }

            bool isChecked = (IsDlgButtonChecked(hWnd, IDC_RESERVE1) == BST_CHECKED);
            g_config.bReserve1 = isChecked;
            int newValue = isChecked ? 100 : 1;
            WriteRemote(g_hProcess, bulletAddr, &newValue, sizeof(int));

            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_RESERVE2) {
            g_config.bReserve2 = (IsDlgButtonChecked(hWnd, IDC_RESERVE2) == BST_CHECKED);
            g_playerIndex = 0;
            g_lockedTeleportTarget = 0;
            g_teleportActive = false;
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_RESERVE3) {
            g_config.bReserve3 = (IsDlgButtonChecked(hWnd, IDC_RESERVE3) == BST_CHECKED);
            g_npcIndex = 0;
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_ROULETTE_FREEZE) {
            g_config.bRouletteFreeze = (IsDlgButtonChecked(hWnd, IDC_ROULETTE_FREEZE) == BST_CHECKED);
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        else if (id == IDC_ROULETTE_INSTANT) {
            g_config.bRouletteInstant = (IsDlgButtonChecked(hWnd, IDC_ROULETTE_INSTANT) == BST_CHECKED);
            InvalidateRect(g_hOverlay, NULL, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_DESTROY: {
        DeleteObject(hTitleFont);
        DeleteObject(hCtrlFont);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK NoticeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow) {
    g_pid = 0;

    g_hGameWnd = FindWindowW(NULL, L"How to Fish");
    if (!g_hGameWnd) {
        g_hGameWnd = FindWindowW(L"UnityWndClass", NULL);
        if (!g_hGameWnd) {
            MessageBoxA(NULL, "找不到游戏窗口，将覆盖全屏", "警告", MB_OK);
            g_hGameWnd = GetDesktopWindow();
        }
    }

    RECT gameRect;
    GetWindowRect(g_hGameWnd, &gameRect);
    int gameX = gameRect.left;
    int gameY = gameRect.top;
    int gameW = gameRect.right - gameRect.left;
    int gameH = gameRect.bottom - gameRect.top;

    // ---- 注册覆盖层窗口类 ----
    WNDCLASSEXW wcOverlay = {};
    wcOverlay.cbSize = sizeof(WNDCLASSEXW);
    wcOverlay.style = CS_HREDRAW | CS_VREDRAW;
    wcOverlay.lpfnWndProc = OverlayWndProc;
    wcOverlay.hInstance = hInst;
    wcOverlay.hbrBackground = NULL;
    wcOverlay.lpszClassName = L"OverlayClass";
    RegisterClassExW(&wcOverlay);

    g_hOverlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"OverlayClass", L"Overlay",
        WS_POPUP,
        gameX, gameY, gameW, gameH,
        NULL, NULL, hInst, NULL
    );
    SetLayeredWindowAttributes(g_hOverlay, RGB(0, 0, 0), 0, LWA_COLORKEY);
    ShowWindow(g_hOverlay, SW_SHOW);
    UpdateWindow(g_hOverlay);

    // ---- 注册主UI窗口类 ----
    WNDCLASSEXW wcUI = {};
    wcUI.cbSize = sizeof(WNDCLASSEXW);
    wcUI.lpfnWndProc = UIWndProc;
    wcUI.hInstance = hInst;
    wcUI.hbrBackground = NULL;
    wcUI.lpszClassName = L"UIControlClass";
    RegisterClassExW(&wcUI);

    HWND hUI = CreateWindowExW(0, L"UIControlClass", L"空明-渔力全开tool-V2.0",
        WS_POPUP | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 440,
        NULL, NULL, hInst, NULL);
    if (!hUI) return -1;

    // ---- 注册公告窗口类 ----
    WNDCLASSEXW wcNotice = {};
    wcNotice.cbSize = sizeof(WNDCLASSEXW);
    wcNotice.style = CS_HREDRAW | CS_VREDRAW;
    wcNotice.lpfnWndProc = NoticeWndProc;
    wcNotice.hInstance = hInst;
    wcNotice.hbrBackground = NULL;
    wcNotice.lpszClassName = L"NoticeClass";
    RegisterClassExW(&wcNotice);

    // ---- 创建公告窗口（高度改为 430） ----
    HWND hNotice = CreateWindowExW(0, L"NoticeClass", L"使用提示",
        WS_POPUP | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 430,   // 高度 430
        NULL, NULL, hInst, NULL);
    if (hNotice) {
        EnableWindow(hUI, FALSE);
        SetWindowLongPtr(hNotice, GWLP_USERDATA, (LONG_PTR)hUI);

        RECT rcMain, rcNotice;
        GetWindowRect(hUI, &rcMain);
        GetWindowRect(hNotice, &rcNotice);
        int width = rcNotice.right - rcNotice.left;
        int height = rcNotice.bottom - rcNotice.top;
        int x = rcMain.left + (rcMain.right - rcMain.left - width) / 2;
        int y = rcMain.top + (rcMain.bottom - rcMain.top - height) / 2;
        SetWindowPos(hNotice, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    int lastX = gameX, lastY = gameY;
    MSG msg;
    int frameCounter = 0;

    // ---- 轮盘状态缓存 ----
    bool lastRouletteInstant = false;
    bool lastRouletteFreeze = false;
    int frozenAwardValue = -1;

    while (g_running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g_running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (g_hGameWnd) {
            GetWindowRect(g_hGameWnd, &gameRect);
            int curX = gameRect.left;
            int curY = gameRect.top;
            int curW = gameRect.right - gameRect.left;
            int curH = gameRect.bottom - gameRect.top;
            if (curX != lastX || curY != lastY) {
                SetWindowPos(g_hOverlay, HWND_TOPMOST, curX, curY, curW, curH, SWP_NOACTIVATE);
                lastX = curX;
                lastY = curY;
            }
        }

        InvalidateRect(g_hOverlay, NULL, FALSE);

        if (g_hOverlay && g_entityArray && g_matrixAddr && g_hProcess) {
            RECT rect;
            GetClientRect(g_hOverlay, &rect);
            DoAimbot(rect.right, rect.bottom);

            // ---- 轮盘功能 ----
            if (g_rouletteAwardAddr && g_rouletteSpeedAddr && g_rouletteInstantAddr) {
                if (g_config.bRouletteFreeze) {
                    if (!lastRouletteFreeze) {
                        int currentAward = 0;
                        if (ReadRemote(g_hProcess, g_rouletteAwardAddr, &currentAward, sizeof(int))) {
                            frozenAwardValue = currentAward;
                        }
                        else {
                            frozenAwardValue = 0;
                        }
                    }
                    if (frozenAwardValue != -1) {
                        WriteRemote(g_hProcess, g_rouletteAwardAddr, &frozenAwardValue, sizeof(int));
                    }
                    float speedVal = 3.0f;
                    WriteRemote(g_hProcess, g_rouletteSpeedAddr, &speedVal, sizeof(float));
                }
                else {
                    if (lastRouletteFreeze) {
                        frozenAwardValue = -1;
                    }
                }
                lastRouletteFreeze = g_config.bRouletteFreeze;

                if (g_config.bRouletteInstant != lastRouletteInstant) {
                    lastRouletteInstant = g_config.bRouletteInstant;
                    int val = g_config.bRouletteInstant ? 257 : 0;
                    WriteRemote(g_hProcess, g_rouletteInstantAddr, &val, sizeof(int));
                }
            }

            // ---- 玩家传送 ----
            if (g_config.bReserve2) {
                static bool bKeyUpPrev = false;
                static bool bKeyDownPrev = false;
                bool bKeyUp = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
                bool bKeyDown = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
                bool bTriggerUp = bKeyUp && !bKeyUpPrev;
                bool bTriggerDown = bKeyDown && !bKeyDownPrev;
                bKeyUpPrev = bKeyUp;
                bKeyDownPrev = bKeyDown;

                if (bTriggerUp || bTriggerDown) {
                    // 1. 找到自己
                    uintptr_t selfEnt = 0;
                    bool hasSelf = false;
                    for (int i = 0; i < MAX_ENTITY_LIMIT; ++i) {
                        uintptr_t entPtr = 0;
                        uintptr_t elemAddr = g_entityArray + i * sizeof(uintptr_t);
                        if (!ReadRemote(g_hProcess, elemAddr, &entPtr, sizeof(uintptr_t))) continue;
                        if (entPtr == 0) continue;
                        DWORD val60 = 0;
                        if (!ReadRemote(g_hProcess, entPtr + 0x60, &val60, sizeof(DWORD))) continue;
                        if (val60 == TARGET_SELF) {
                            if (!ReadRemote(g_hProcess, entPtr + 0x28, &selfEnt, sizeof(uintptr_t))) break;
                            if (selfEnt != 0) hasSelf = true;
                            break;
                        }
                    }
                    if (!hasSelf) {
                        g_lockedTeleportTarget = 0;
                        g_teleportActive = false;
                    }
                    else {
                        // 2. 收集所有玩家实体（排除自己）
                        std::vector<uintptr_t> targetList;
                        for (int i = 0; i < MAX_ENTITY_LIMIT; ++i) {
                            uintptr_t entPtr = 0;
                            uintptr_t elemAddr = g_entityArray + i * sizeof(uintptr_t);
                            if (!ReadRemote(g_hProcess, elemAddr, &entPtr, sizeof(uintptr_t))) continue;
                            if (entPtr == 0) continue;
                            DWORD val60 = 0;
                            if (!ReadRemote(g_hProcess, entPtr + 0x60, &val60, sizeof(DWORD))) continue;
                            if (val60 == TARGET_PLAYER) {
                                uintptr_t ent = 0;
                                if (!ReadRemote(g_hProcess, entPtr + 0x28, &ent, sizeof(uintptr_t))) continue;
                                if (ent != 0 && ent != selfEnt) {
                                    targetList.push_back(ent);
                                }
                            }
                        }

                        if (!targetList.empty()) {
                            // 切换索引
                            if (bTriggerUp) {
                                g_playerIndex--;
                                if (g_playerIndex < 0) g_playerIndex = (int)targetList.size() - 1;
                            }
                            else if (bTriggerDown) {
                                g_playerIndex++;
                                if (g_playerIndex >= (int)targetList.size()) g_playerIndex = 0;
                            }

                            // 执行传送
                            uintptr_t targetEnt = targetList[g_playerIndex];
                            Vector3 targetPos;
                            if (ReadRemote(g_hProcess, targetEnt + 0xA0, &targetPos, sizeof(Vector3))) {
                                Vector3 newPos = targetPos;
                                newPos.y += 1.5f;
                                newPos.x -= 0.5f;
                                newPos.z -= 0.5f;
                                WriteRemote(g_hProcess, selfEnt + 0xA0, &newPos, sizeof(Vector3));
                            }
                        }
                    }
                }
            }
            else {
                g_lockedTeleportTarget = 0;
                g_teleportActive = false;
            }

            // ---- NPC传送 ----
            if (g_config.bReserve3) {
                static bool bKeyLeftPrev = false;
                static bool bKeyRightPrev = false;
                bool bKeyLeft = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
                bool bKeyRight = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
                bool bTriggerLeft = bKeyLeft && !bKeyLeftPrev;
                bool bTriggerRight = bKeyRight && !bKeyRightPrev;
                bKeyLeftPrev = bKeyLeft;
                bKeyRightPrev = bKeyRight;

                if (bTriggerLeft || bTriggerRight) {
                    uintptr_t selfEnt = 0;
                    bool hasSelf = false;
                    for (int i = 0; i < MAX_ENTITY_LIMIT; ++i) {
                        uintptr_t entPtr = 0;
                        uintptr_t elemAddr = g_entityArray + i * sizeof(uintptr_t);
                        if (!ReadRemote(g_hProcess, elemAddr, &entPtr, sizeof(uintptr_t))) continue;
                        if (entPtr == 0) continue;
                        DWORD val60 = 0;
                        if (!ReadRemote(g_hProcess, entPtr + 0x60, &val60, sizeof(DWORD))) continue;
                        if (val60 == TARGET_SELF) {
                            if (!ReadRemote(g_hProcess, entPtr + 0x28, &selfEnt, sizeof(uintptr_t))) break;
                            if (selfEnt != 0) hasSelf = true;
                            break;
                        }
                    }
                    if (hasSelf) {
                        std::vector<uintptr_t> npcList;
                        for (int i = 0; i < MAX_ENTITY_LIMIT; ++i) {
                            uintptr_t entPtr = 0;
                            uintptr_t elemAddr = g_entityArray + i * sizeof(uintptr_t);
                            if (!ReadRemote(g_hProcess, elemAddr, &entPtr, sizeof(uintptr_t))) continue;
                            if (entPtr == 0) continue;
                            DWORD val60 = 0;
                            float val9C = 0.0f;
                            if (!ReadRemote(g_hProcess, entPtr + 0x60, &val60, sizeof(DWORD))) continue;
                            if (!ReadRemote(g_hProcess, entPtr + 0x9C, &val9C, sizeof(float))) continue;
                            if (val60 == TARGET_NPC && fabs(val9C - 0.17f) < 0.02f) {
                                uintptr_t ent = 0;
                                if (!ReadRemote(g_hProcess, entPtr + 0x28, &ent, sizeof(uintptr_t))) continue;
                                if (ent != 0) npcList.push_back(ent);
                            }
                        }
                        if (!npcList.empty()) {
                            if (bTriggerLeft) {
                                g_npcIndex--;
                                if (g_npcIndex < 0) g_npcIndex = (int)npcList.size() - 1;
                            }
                            else if (bTriggerRight) {
                                g_npcIndex++;
                                if (g_npcIndex >= (int)npcList.size()) g_npcIndex = 0;
                            }
                            uintptr_t targetEnt = npcList[g_npcIndex];
                            Vector3 npcPos;
                            if (ReadRemote(g_hProcess, targetEnt + 0xA0, &npcPos, sizeof(Vector3))) {
                                WriteRemote(g_hProcess, selfEnt + 0xA0, &npcPos, sizeof(Vector3));
                            }
                        }
                    }
                }
            }
        }

        Sleep(1);
    }

    if (g_hProcess) {
        CloseHandle(g_hProcess);
        g_hProcess = NULL;
    }
    return 0;
}

LRESULT CALLBACK NoticeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HWND hBtn = CreateWindowW(L"BUTTON", L"我知道了",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            10, 360, 100, 30, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);
        SetFocus(hBtn);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // 背景
        HBRUSH bgBrush = CreateSolidBrush(RGB(245, 248, 255));
        FillRect(hdc, &rc, bgBrush);
        DeleteObject(bgBrush);

        // 标题栏
        RECT titleRc = { 0, 0, rc.right, 36 };
        HBRUSH titleBrush = CreateSolidBrush(RGB(43, 122, 236));
        FillRect(hdc, &titleRc, titleBrush);
        DeleteObject(titleBrush);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        HFONT hTitleFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT oldFont = (HFONT)SelectObject(hdc, hTitleFont);
        DrawTextW(hdc, L"使用提示", -1, &titleRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        DeleteObject(hTitleFont);

        // 正文（字体22）
        SetTextColor(hdc, RGB(0, 0, 0));
        HFONT hTextFont = CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        oldFont = (HFONT)SelectObject(hdc, hTextFont);
        RECT textRc = { 10, 46, rc.right - 10, rc.bottom - 10 };
        const wchar_t* text =
            L"• 1.先获取地址和初始化绘制再开功能\n"
            L"• 2.如遇获取失败但功能正常使用请忽略，无效尝试重启游戏\n"
            L"• 3.推荐使用鱼类海鸥搭配过滤动态物体\n"
            L"• 4.美国子弹为100倍子弹数量，需要手持枪械开启，每个枪都是独立的\n"
            L"• 5.冻结轮盘奖项为冻结上一次的正确色号，例：正常游戏抽到了红色，开启冻结后红色一直为正确色号\n"
            L"• 6.轮盘立即结算为执行上一次的正确或失败，例：上一把成功了，那么执行立即结算会直接成功，该功能也可以用来解决冻结轮盘奖项卡住问题\n"
            L"• 7.玩家传送和NPC传送由方向键控制↑↓←→上一个/下一个\n"
            L"• 8.无线金币开启后不会显示金币数量改变但可以购买物品\n"
            L"• 9.金币功能、轮盘功能仅单机或房主可用，轮盘功能房主使用后成员也可使用";
        DrawTextW(hdc, text, -1, &textRc, DT_LEFT | DT_WORDBREAK);
        SelectObject(hdc, oldFont);
        DeleteObject(hTextFont);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 1) {
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_DESTROY: {
        HWND hMain = (HWND)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (hMain) {
            EnableWindow(hMain, TRUE);
            SetForegroundWindow(hMain);
        }
        return 0;
    }
    case WM_NCHITTEST: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (pt.y < 36) {
            return HTCAPTION;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
