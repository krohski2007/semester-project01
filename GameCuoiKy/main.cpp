#include "raylib.h"
#include <vector>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <functional>

// --- CẤU HÌNH TRÒ CHƠI ---
const int SW = 1800, SH = 800;
const float PSPD = 220.f;  
const float BSPD = 520.f;  
const float BRAD = 6.f;    
const float PRAD = 18.f;   
const char* SFILE = "scores.txt";

enum GameState { MENU, PLAYING, PAUSED, GAMEOVER, LEADERBOARD };
enum EnemyType { NORMAL, BOMBER, LASER, BEAM };
enum WeaponType { GUN, SWORD };

struct Bullet { 
    Vector2 pos, vel; 
    bool active = true; 
};

struct Enemy {
    Vector2 pos; 
    EnemyType type;
    float hp, maxHp, speed;
    bool alive = true;
    float hitFlash = 0;
    
    float stateT = 0; 
    int state = 0; 
    Vector2 aimDir = {0, 0};
};

struct Player {
    Vector2 pos = {SW / 2.f, SH / 2.f};
    float hp = 100, maxHp = 100, shootCD = 0, iTimer = 0;
    WeaponType wep = GUN;
    float swordSwingT = 0;
};

struct Explosion { 
    Vector2 pos; 
    float maxR, t, dur; 
};

// --- BIẾN TOÀN CỤC ---
GameState gState = MENU;
Player player;
std::vector<Bullet> bullets;
std::vector<Enemy> enemies;
std::vector<Explosion> exps;
std::vector<int> scores;

int score = 0, wave = 1, enemiesToSpawn = 0;
float spawnT = 0, shake = 0, gameTime = 0;
float camZoom = 1.0f; 

Vector2 playerDir = {0, 0};
Vector2 camShift = {0, 0};

// 1. QUẢN LÝ ĐIỂM SỐ & GIAO DIỆN
void LoadScores() {
    scores.clear(); 
    std::ifstream f(SFILE); 
    int s;
    while (f >> s) scores.push_back(s);
    std::sort(scores.begin(), scores.end(), std::greater<int>());
}

void SaveScore(int ns) {
    scores.push_back(ns); 
    std::sort(scores.begin(), scores.end(), std::greater<int>());
    if (scores.size() > 10) scores.resize(10);
    
    std::ofstream f(SFILE); 
    for (int s : scores) f << s << "\n";
}

bool BtnClick(Rectangle r) { 
    return CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON); 
}

void DrawBtn(Rectangle r, const char* txt, Color col) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRec(r, hover ? ColorAlpha(col, 0.8f) : col);
    DrawRectangleLinesEx(r, 2, WHITE);
    DrawText(txt, (int)(r.x + r.width / 2 - MeasureText(txt, 20) / 2), (int)(r.y + r.height / 2 - 10), 20, WHITE);
}

// 2. LOGIC TRÒ CHƠI (CẬP NHẬT)
void ResetGame() {
    player = Player(); 
    bullets.clear(); 
    enemies.clear(); 
    exps.clear();
    score = 0; 
    wave = 1; 
    enemiesToSpawn = 5; 
    spawnT = 0;
    camShift = {0, 0};
    camZoom = 1.0f;
}

void SpawnEnemy() {
    Enemy e; 
    int r = GetRandomValue(0, 100);
    
    if (wave < 2) {
        e.type = NORMAL;
    } else {
        if (r < 50) e.type = NORMAL;
        else if (r < 75) e.type = BOMBER;
        else if (r < 90) e.type = LASER;
        else e.type = BEAM;
    }

    int side = GetRandomValue(0, 3);
    if (side == 0) e.pos = {(float)GetRandomValue(0, SW), -30.f};
    else if (side == 1) e.pos = {(float)GetRandomValue(0, SW), (float)SH + 30};
    else if (side == 2) e.pos = {-30.f, (float)GetRandomValue(0, SH)};
    else e.pos = {(float)SW + 30, (float)GetRandomValue(0, SH)};

    float hpMult = 1.0f + (wave - 1) * 0.2f;
    if (e.type == NORMAL) { e.hp = e.maxHp = 40 * hpMult; e.speed = 90 + wave * 2; }
    else if (e.type == BOMBER) { e.hp = e.maxHp = 25 * hpMult; e.speed = 150 + wave * 3; }
    else if (e.type == LASER) { e.hp = e.maxHp = 70 * hpMult; e.speed = 50; }
    else { e.hp = e.maxHp = 120 * hpMult; e.speed = 40; } 
    
    enemies.push_back(e);
}

void UpdatePlayer(float dt) {
    Vector2 d = {0, 0};
    if (IsKeyDown(KEY_W)) d.y -= 1; 
    if (IsKeyDown(KEY_S)) d.y += 1;
    if (IsKeyDown(KEY_A)) d.x -= 1; 
    if (IsKeyDown(KEY_D)) d.x += 1;
    
    if (d.x != 0 || d.y != 0) { 
        float len = sqrtf(d.x * d.x + d.y * d.y); 
        playerDir = {d.x / len, d.y / len};
        player.pos.x += playerDir.x * PSPD * dt; 
        player.pos.y += playerDir.y * PSPD * dt; 
    } else {
        playerDir = {0, 0};
    }
    
    player.pos.x = fmaxf(PRAD, fminf(player.pos.x, SW - PRAD));
    player.pos.y = fmaxf(PRAD, fminf(player.pos.y, SH - PRAD));

    if (IsKeyPressed(KEY_F)) {
        player.wep = (player.wep == GUN) ? SWORD : GUN;
    }
    
    if (player.shootCD > 0) player.shootCD -= dt;
    if (player.swordSwingT > 0) player.swordSwingT -= dt;
    if (player.iTimer > 0) player.iTimer -= dt;

    bool isShooting = IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || (player.wep == GUN && IsMouseButtonDown(MOUSE_LEFT_BUTTON));
    
    if (isShooting) {
        if (player.wep == GUN && player.shootCD <= 0) {
            player.shootCD = 0.15f;
            Vector2 m = GetMousePosition();
            m.x = (m.x - camShift.x) / camZoom;
            m.y = (m.y - camShift.y) / camZoom;
            
            float dx = m.x - player.pos.x, dy = m.y - player.pos.y;
            float dd = sqrtf(dx * dx + dy * dy);
            
            if (dd > 0) {
                Vector2 velocity = {(dx / dd) * BSPD, (dy / dd) * BSPD};
                bullets.push_back({player.pos, velocity, true});
            }
        } 
        else if (player.wep == SWORD && player.swordSwingT <= 0) {
            player.swordSwingT = 0.3f;
            Vector2 m = GetMousePosition();
            m.x = (m.x - camShift.x) / camZoom;
            m.y = (m.y - camShift.y) / camZoom;
            
            float angMouse = atan2f(m.y - player.pos.y, m.x - player.pos.x);
            
            for (auto& e : enemies) {
                if (!e.alive) continue;
                float dx = e.pos.x - player.pos.x, dy = e.pos.y - player.pos.y;
                float dist = sqrtf(dx * dx + dy * dy);
                float angE = atan2f(dy, dx);
                
                float diff = angE - angMouse;
                while (diff > PI) diff -= 2 * PI; 
                while (diff < -PI) diff += 2 * PI;
                
                if (dist < 110 && fabsf(diff) < PI / 1.5f) { 
                    e.hp -= 30; 
                    e.hitFlash = 0.15f; 
                    if (e.hp <= 0) { e.alive = false; score += 15; }
                }
            }
        }
    }
}

void UpdateBullets(float dt) {
    for (auto& b : bullets) {
        if (!b.active) continue;
        b.pos.x += b.vel.x * dt;
        b.pos.y += b.vel.y * dt;
        if (b.pos.x < -100 || b.pos.x > SW + 100 || b.pos.y < -100 || b.pos.y > SH + 100) b.active = false;
    }
}

void UpdateEnemies(float dt) {
    for (auto& e : enemies) {
        if (!e.alive) continue;
        float dx = player.pos.x - e.pos.x, dy = player.pos.y - e.pos.y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (e.hitFlash > 0) e.hitFlash -= dt;

        if (e.type == NORMAL || e.type == BOMBER) {
            if (dist > 0) { 
                e.pos.x += (dx / dist) * e.speed * dt; 
                e.pos.y += (dy / dist) * e.speed * dt; 
            }
            if (e.type == NORMAL && dist < PRAD + 20 && player.iTimer <= 0) { 
                player.hp -= 10; player.iTimer = 1.f; shake = 0.3f; 
            }
            else if (e.type == BOMBER && dist < 60) { 
                e.alive = false; 
                exps.push_back({e.pos, 90, 0.5f, 0.5f}); 
                if (dist < 90 && player.iTimer <= 0) { 
                    player.hp -= 25; player.iTimer = 0.8f; shake = 0.5f; 
                } 
            }
        }
        else if (e.type == BEAM || e.type == LASER) {
            e.stateT += dt;
            if (e.state == 0) { 
                if (dist > 300) { 
                    e.pos.x += (dx / dist) * e.speed * dt; 
                    e.pos.y += (dy / dist) * e.speed * dt; 
                }
                if (e.stateT > 2.0f) { 
                    e.state = 1; e.stateT = 0; e.aimDir = {dx / dist, dy / dist}; 
                }
            } else if (e.state == 1) { 
                if (e.stateT > (e.type == BEAM ? 1.2f : 0.8f)) { 
                    e.state = 2; e.stateT = 0; 
                }
            } else if (e.state == 2) { 
                //Tính sát thương cho BEAM và LASER
                Vector2 tp = {player.pos.x - e.pos.x, player.pos.y - e.pos.y};
                float dot = tp.x * e.aimDir.x + tp.y * e.aimDir.y;
                float beamDist = sqrtf(powf(tp.x - dot * e.aimDir.x, 2) + powf(tp.y - dot * e.aimDir.y, 2));
                
                if (dot > 0) {
                    float hitRad = (e.type == BEAM) ? 30.0f : 10.0f; // Bán kính trúng đòn theo loại
                    float dmg = (e.type == BEAM) ? 50.0f : 25.0f;    // Sát thương theo loại
                    
                    if (beamDist < hitRad && player.iTimer <= 0) { 
                        player.hp -= dmg * dt; 
                        shake = fmaxf(shake, (e.type == BEAM) ? 0.2f : 0.1f); 
                    } else if (beamDist < 150) {
                        shake = fmaxf(shake, 0.05f);
                    }
                }
                
                if (e.stateT > 0.6f) { e.state = 0; e.stateT = 0; }
            }
        }
    }
}

void CheckCollisions() {
    for (auto& b : bullets) {
        if (!b.active) continue;
        for (auto& e : enemies) {
            if (e.alive && CheckCollisionCircles(b.pos, BRAD, e.pos, 22)) {
                e.hp -= 10; e.hitFlash = 0.15f; b.active = false; 
                if (e.hp <= 0) { e.alive = false; score += 10; }
            }
        }
    }
}

void CleanUpDeadEntities(float dt) {
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b){ return !b.active; }), bullets.end());
    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const Enemy& e){ return !e.alive; }), enemies.end());
    for (auto& ex : exps) ex.t -= dt; 
    exps.erase(std::remove_if(exps.begin(), exps.end(), [](const Explosion& x){ return x.t <= 0; }), exps.end());
}


// 3. VẼ ĐỒ HỌA
void DrawSlime(Vector2 p, float r, Color c, float flash) {
    DrawCircleV(p, r, flash > 0 ? WHITE : c);
    DrawCircleV(p, r * 0.7f, ColorBrightness(c, -0.2f));
    DrawCircle(p.x - r * 0.3f, p.y - r * 0.2f, r * 0.15f, WHITE);
    DrawCircle(p.x + r * 0.3f, p.y - r * 0.2f, r * 0.15f, WHITE);
    DrawCircle(p.x - r * 0.3f, p.y - r * 0.2f, r * 0.07f, BLACK);
    DrawCircle(p.x + r * 0.3f, p.y - r * 0.2f, r * 0.07f, BLACK);
}

void DrawGame() {
    for (int x = -3000; x < SW + 3000; x += 48) {
        for (int y = -3000; y < SH + 3000; y += 48) {
            int gridX = (x + 3000) / 48;
            int gridY = (y + 3000) / 48;
            DrawRectangle(x, y, 48, 48, ((gridX + gridY) % 2 == 0) ? Color{60, 110, 50, 255} : Color{50, 100, 40, 255});
        }
    }

    for (auto& b : bullets) DrawCircleV(b.pos, BRAD, YELLOW);
    for (auto& ex : exps) DrawCircleV(ex.pos, ex.maxR * (1 - ex.t / ex.dur * 0.5f), {255, 160, 0, (unsigned char)(ex.t / ex.dur * 180)});

    for (auto& e : enemies) {
        if (!e.alive) continue;
        Color col = (e.type == NORMAL) ? PURPLE : (e.type == BOMBER ? RED : (e.type == LASER ? BLUE : ORANGE));
        DrawSlime(e.pos, (e.type == BEAM ? 28.f : 22.f), col, e.hitFlash);
        
        float hpPct = fmaxf(0.0f, e.hp / e.maxHp);
        DrawRectangle(e.pos.x - 18, e.pos.y - 35, 36, 6, MAROON);
        DrawRectangle(e.pos.x - 18, e.pos.y - 35, (int)(36 * hpPct), 6, LIME);
        DrawRectangleLines(e.pos.x - 18, e.pos.y - 35, 36, 6, BLACK);
        
        // VFX: Nhấp nháy nhanh dần khi ngắm và shake khi bắn
        if (e.state == 1) {
            float flash = (sinf(gameTime * 30.0f * e.stateT) + 1.0f) * 0.5f;
            DrawLineEx(e.pos, {e.pos.x + e.aimDir.x * 1000, e.pos.y + e.aimDir.y * 1000}, 2 + flash * 2, ColorAlpha(RED, 0.4f + flash * 0.6f)); 
        }
        if (e.state == 2) {
            Vector2 end = {e.pos.x + e.aimDir.x * 1000, e.pos.y + e.aimDir.y * 1000};
            float pulse = (sinf(gameTime * 60.0f) + 1.0f) * 0.5f; // Xung động năng lượng
            
            if (e.type == BEAM) {
                DrawLineEx(e.pos, end, 60 + pulse * 20, ColorAlpha(col, 0.4f));
                DrawLineEx(e.pos, end, 40 + pulse * 10, col);
                DrawLineEx(e.pos, end, 20, WHITE);
                DrawCircleV(e.pos, 35 + pulse * 10, ColorAlpha(col, 0.7f)); // Lõi phát sáng
                DrawCircleV(e.pos, 20, WHITE);
            } else { // Hiệu ứng cho LASER nhỏ
                DrawLineEx(e.pos, end, 20 + pulse * 10, ColorAlpha(col, 0.4f));
                DrawLineEx(e.pos, end, 10 + pulse * 5, col);
                DrawLineEx(e.pos, end, 4, WHITE);
                DrawCircleV(e.pos, 15 + pulse * 10, ColorAlpha(col, 0.7f)); // Lõi phát sáng
                DrawCircleV(e.pos, 8, WHITE);
            }
        }
    }

    Vector2 m = GetMousePosition();
    m.x = (m.x - camShift.x) / camZoom;
    m.y = (m.y - camShift.y) / camZoom;
    float ang = atan2f(m.y - player.pos.y, m.x - player.pos.x);
    
    if (player.iTimer <= 0 || (int)(gameTime * 10) % 2 == 0) {
        DrawSlime(player.pos, PRAD, SKYBLUE, 0);
        if (player.wep == SWORD && player.swordSwingT > 0) {
            float prog = 1.0f - (player.swordSwingT / 0.3f);
            float startAng = ang - 1.5f, curAng = startAng + prog * 3.0f;
            DrawCircleSector(player.pos, 75, startAng * RAD2DEG, curAng * RAD2DEG, 20, ColorAlpha(GOLD, 1.0f - prog));
            DrawCircleSector(player.pos, 55, startAng * RAD2DEG, curAng * RAD2DEG, 20, ColorAlpha(WHITE, (1.0f - prog) * 0.5f));
            DrawLineEx(player.pos, {player.pos.x + cosf(curAng) * 65, player.pos.y + sinf(curAng) * 65}, 8, WHITE);
            DrawLineEx(player.pos, {player.pos.x + cosf(curAng) * 65, player.pos.y + sinf(curAng) * 65}, 4, YELLOW);
        }
    }
}

void DrawHUD() {
    DrawRectangle(20, 20, 200, 20, DARKGRAY); 
    DrawRectangle(20, 20, (int)(200 * (player.hp / player.maxHp)), 20, (player.hp < 30 ? RED : GREEN));
    DrawRectangleLines(20, 20, 200, 20, WHITE);
    DrawText(TextFormat("WAVE: %d | ENEMIES LEFT: %d", wave, (int)enemies.size() + enemiesToSpawn), 20, 50, 22, WHITE);
    DrawText(TextFormat("SCORE: %d", score), 20, 80, 20, YELLOW);
    DrawText(player.wep == GUN ? "[F] GUN" : "[F] SWORD", 20, 110, 20, SKYBLUE);
}

// 4. VÒNG LẶP CHÍNH 
int main() {
    InitWindow(SW, SH, "Project cuối kỳ"); 
    SetTargetFPS(60); 
    LoadScores();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime(); 
        gameTime += dt; 
        if (shake > 0) shake -= dt * 2;

        if (gState == MENU) { 
            if (BtnClick({SW / 2 - 100, 300, 200, 50})) { ResetGame(); gState = PLAYING; } 
            if (BtnClick({SW / 2 - 100, 370, 200, 50})) gState = LEADERBOARD; 
        }
        else if (gState == LEADERBOARD) { 
            if (BtnClick({SW / 2 - 100, 600, 200, 50})) gState = MENU; 
        }
        else if (gState == GAMEOVER) { 
            if (BtnClick({SW / 2 - 100, 420, 200, 50})) { ResetGame(); gState = PLAYING; } 
            if (BtnClick({SW / 2 - 100, 490, 200, 50})) { ResetGame(); gState = MENU; }
        }
        else if (gState == PAUSED) { 
            if (IsKeyPressed(KEY_ESCAPE) || BtnClick({SW / 2 - 100, 300, 200, 50})) gState = PLAYING; 
            if (BtnClick({SW / 2 - 100, 370, 200, 50})) { ResetGame(); gState = MENU; }
        }
        else if (gState == PLAYING) {
            camZoom += GetMouseWheelMove() * 0.1f;
            camZoom = fmaxf(0.5f, fminf(camZoom, 3.0f));

            UpdatePlayer(dt); UpdateBullets(dt); UpdateEnemies(dt);
            CheckCollisions(); CleanUpDeadEntities(dt);
            
            camShift.x += ((-playerDir.x * 25.0f) - camShift.x) * dt * 6.0f;
            camShift.y += ((-playerDir.y * 25.0f) - camShift.y) * dt * 6.0f;
            
            spawnT += dt; 
            if (spawnT > 1.0f && enemiesToSpawn > 0) { 
                spawnT = 0; SpawnEnemy(); enemiesToSpawn--; 
            }
            if (enemiesToSpawn == 0 && enemies.size() == 0) { 
                wave++; enemiesToSpawn = 5 + wave * 2; 
            }
            if (player.hp <= 0) { 
                SaveScore(score); LoadScores(); gState = GAMEOVER; 
            }
            if (IsKeyPressed(KEY_ESCAPE)) gState = PAUSED;
        }

        BeginDrawing(); 
        ClearBackground(BLACK);
        
        Camera2D camera = { 0 };
        camera.offset = { (shake > 0 ? (float)GetRandomValue(-5, 5) * shake : 0) + camShift.x, 
                          (shake > 0 ? (float)GetRandomValue(-5, 5) * shake : 0) + camShift.y };
        camera.zoom = camZoom;
        
        BeginMode2D(camera);
        if (gState == PLAYING || gState == PAUSED) DrawGame();
        EndMode2D(); 
        
        if (gState == PLAYING) DrawHUD();
        else if (gState == PAUSED) {
            DrawHUD();
            DrawRectangle(0, 0, SW, SH, {0, 0, 0, 150});
            DrawText("PAUSED", SW / 2 - MeasureText("PAUSED", 40) / 2, 200, 40, WHITE);
            DrawBtn({SW / 2 - 100, 300, 200, 50}, "RESUME", LIME);
            DrawBtn({SW / 2 - 100, 370, 200, 50}, "MAIN MENU", RED);
        }
        else if (gState == MENU) { 
            DrawRectangle(0, 0, SW, SH, DARKGREEN); 
            DrawText("SLIME SURVIVAL", SW / 2 - 180, 150, 45, WHITE); 
            DrawBtn({SW / 2 - 100, 300, 200, 50}, "PLAY", LIME); 
            DrawBtn({SW / 2 - 100, 370, 200, 50}, "SCORES", GOLD); 
        }
        else if (gState == LEADERBOARD) { 
            DrawText("TOP 10", SW / 2 - 50, 100, 30, GOLD); 
            for (int i = 0; i < (int)scores.size() && i < 10; i++)
                DrawText(TextFormat("%d. %d", i + 1, scores[i]), SW / 2 - 40, 150 + i * 35, 25, WHITE); 
            DrawBtn({SW / 2 - 100, 600, 200, 50}, "BACK", RED); 
        }
        else if (gState == GAMEOVER) { 
            DrawRectangle(0, 0, SW, SH, {20, 0, 0, 200});
            DrawText("GAME OVER", SW / 2 - 120, 180, 45, RED); 
            DrawText(TextFormat("SCORE: %d", score), SW / 2 - 60, 250, 30, WHITE); 
            DrawBtn({SW / 2 - 100, 420, 200, 50}, "RETRY", LIME); 
            DrawBtn({SW / 2 - 100, 490, 200, 50}, "MAIN MENU", RED); 
        }
        EndDrawing();
    }
    CloseWindow(); 
    return 0;
}