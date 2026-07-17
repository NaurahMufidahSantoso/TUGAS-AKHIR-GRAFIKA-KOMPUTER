#include <GL/freeglut.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <vector>
#include <string>
#include <fstream>
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

using namespace std;

// WINDOW & STATE
int WIN_W = 800, WIN_H = 600;
enum GameState { MENU, PLAYING, GAMEOVER };
GameState state = MENU;

// PLAYER
float playerX = 400, playerY = 100;
const float PLAYER_SIZE = 18.0f;
const float PLAYER_SPEED = 5.0f;
int   maxHP = 5, hp = 5;
int   invulnTimer = 0;          // kebal sesaat setelah kena hit
int   fireCooldown = 0;
const int FIRE_DELAY = 12;
bool  keyUp=false, keyDown=false, keyLeft=false, keyRight=false, keySpace=false;

// BULLET
struct Bullet { float x, y, vx, vy; bool active; bool fromPlayer; bool powered; };
vector<Bullet> bullets;

// POWER-UP SENJATA SEMENTARA (tembakan tripel selama 20 detik)
struct WeaponCrate { float x, y; bool active; };
vector<WeaponCrate> weaponCrates;
int weaponSpawnTimer = 0, nextWeaponSpawn = 500;
float weaponTimer = 0.0f;
const float WEAPON_DURATION = 625.0f;  // ~10 detik (timer jalan tiap 16ms => ~62.5 frame/detik)
const int   WEAPON_RESPAWN_DELAY = 1875; // ~30 detik setelah senjata terpakai

// ENEMY
struct Enemy {
    float x, y;
    float vx, vy;
    int type;      // 0 = drone, 1 = ufo
    int hp;
    int shootTimer;
    float phase;   // untuk gerak zig-zag
    bool alive;
};
vector<Enemy> enemies;

// CHEST (power-up pengisi HP)
struct Chest { float x, y; bool active; };
vector<Chest> chests;
int chestSpawnTimer = 0;

// HUTAN LATAR BELAKANG (banyak pohon, berlapis, bisa bergerak/scroll)
struct ForestTree {
    float x, y;
    float scale;
    int layer;   // 0 = jauh/kecil/gelap, 1 = tengah, 2 = dekat/besar/terang
    int shape;   // variasi bentuk pohon (0,1,2)
};
vector<ForestTree> forest;

// DUNIA LAUT & KEPULAUAN (latar baru)
struct Island  { float x, y, size; };
struct Islet   { float x, y, size; bool hasTree; };
struct Sparkle { float x, y, phase; };
struct CornerCloud { float x, y, scale; };

vector<Island> islands;
vector<Islet> islets;
vector<Sparkle> sparkles;
vector<CornerCloud> cornerClouds;

// EFEK VISUAL (ledakan, dsb)
struct Particle { float x, y, vx, vy; float life, maxLife; float r,g,b; };
vector<Particle> particles;
int runFrame = 0;

void spawnExplosion(float x, float y, float r, float g, float b, int count) {
    for (int i = 0; i < count; i++) {
        Particle p;
        p.x = x; p.y = y;
        float ang = (float)(rand()%628)/100.0f;
        float spd = 1.0f + (rand()%300)/100.0f;
        p.vx = cosf(ang)*spd; p.vy = sinf(ang)*spd;
        p.maxLife = 22 + rand()%14; p.life = p.maxLife;
        p.r=r; p.g=g; p.b=b;
        particles.push_back(p);
    }
}

void spawnTrail(float x, float y, float r, float g, float b) {
    Particle p;
    p.x = x + (rand()%7-3); p.y = y + (rand()%7-3);
    p.vx = (rand()%40-20)/100.0f; p.vy = -1.2f - (rand()%50)/100.0f;
    p.maxLife = 14 + rand()%8; p.life = p.maxLife;
    p.r=r; p.g=g; p.b=b;
    particles.push_back(p);
}

// TEKS MELAYANG (skor/+HP) & GONCANGAN LAYAR (screen shake)
struct FloatingText { float x, y; string text; float life, maxLife; float r,g,b; };
vector<FloatingText> floatingTexts;
float shakeTime = 0, shakeMag = 0;

void spawnFloatText(float x, float y, const string &txt, float r, float g, float b) {
    FloatingText f; f.x=x; f.y=y; f.text=txt; f.maxLife=45; f.life=45; f.r=r; f.g=g; f.b=b;
    floatingTexts.push_back(f);
}

void triggerShake(float mag, float time) {
    shakeMag = mag; shakeTime = time;
}

// SUARA (di-generate langsung di memori sbg WAV PCM, tanpa file eksternal)
vector<unsigned char> shootSoundData;
vector<unsigned char> explosionSoundData;

vector<unsigned char> makeWavBuffer(const vector<short>& samples, int sampleRate = 44100) {
    int dataSize = (int)samples.size() * 2;
    int chunkSize = 36 + dataSize;
    vector<unsigned char> buf(44 + dataSize);
    memcpy(&buf[0], "RIFF", 4);
    memcpy(&buf[4], &chunkSize, 4);
    memcpy(&buf[8], "WAVE", 4);
    memcpy(&buf[12], "fmt ", 4);
    int fmtSize = 16; memcpy(&buf[16], &fmtSize, 4);
    short audioFmt = 1; memcpy(&buf[20], &audioFmt, 2);
    short channels = 1; memcpy(&buf[22], &channels, 2);
    memcpy(&buf[24], &sampleRate, 4);
    int byteRate = sampleRate * 2; memcpy(&buf[28], &byteRate, 4);
    short blockAlign = 2; memcpy(&buf[32], &blockAlign, 2);
    short bitsPerSample = 16; memcpy(&buf[34], &bitsPerSample, 2);
    memcpy(&buf[36], "data", 4);
    memcpy(&buf[40], &dataSize, 4);
    if (dataSize > 0) memcpy(&buf[44], samples.data(), dataSize);
    return buf;
}

// Suara tembak: nada pendek melengking turun (pew!)
vector<unsigned char> genShootSound() {
    int sr = 44100; float dur = 0.09f; int n = (int)(sr * dur);
    vector<short> s(n);
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;
        float freq = 950.0f - 5200.0f * t;
        if (freq < 220.0f) freq = 220.0f;
        float env = 1.0f - (float)i / n;
        float val = sinf(2.0f * 3.14159265f * freq * t) * env * 0.4f;
        s[i] = (short)(val * 32767);
    }
    return makeWavBuffer(s, sr);
}

// Suara ledakan: campuran noise + gemuruh rendah, meluruh cepat
vector<unsigned char> genExplosionSound() {
    int sr = 44100; float dur = 0.42f; int n = (int)(sr * dur);
    vector<short> s(n);
    unsigned int seed = 987654321u;
    for (int i = 0; i < n; i++) {
        float t = (float)i / sr;
        float env = powf(1.0f - (float)i / n, 1.6f);
        seed = seed * 1664525u + 1013904223u;
        float noise = ((float)(seed % 2000) / 1000.0f) - 1.0f;
        float rumble = sinf(2.0f * 3.14159265f * 65.0f * t);
        float val = (noise * 0.65f + rumble * 0.35f) * env * 0.65f;
        s[i] = (short)(val * 32767);
    }
    return makeWavBuffer(s, sr);
}

void initSounds() {
    shootSoundData = genShootSound();
    explosionSoundData = genExplosionSound();
}

void playShootSound() {
    if (!shootSoundData.empty())
        PlaySoundA((LPCSTR)shootSoundData.data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void playExplosionSound() {
    if (!explosionSoundData.empty())
        PlaySoundA((LPCSTR)explosionSoundData.data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// WAVE & SKOR
int wave = 1;
int enemiesToSpawnThisWave = 0;
int enemySpawnTimer = 0;
int score = 0;
int bestScore = 0;
int waveAnnounceTimer = 0; // tampilkan teks "WAVE X" sesaat

// PROGRESI WAVE BERDASARKAN SKOR (naik level tiap 200 poin)
const int SCORE_PER_WAVE = 200;
int nextWaveScore = SCORE_PER_WAVE;

// HIGH SCORE PERSISTEN (disimpan ke file agar tidak hilang saat game ditutup)
const char* HIGHSCORE_FILE = "highscore.txt";

void loadBestScore() {
    ifstream fin(HIGHSCORE_FILE);
    if (fin.is_open()) {
        int v = 0;
        if (fin >> v) bestScore = v;
        fin.close();
    }
}

void saveBestScore() {
    ofstream fout(HIGHSCORE_FILE);
    if (fout.is_open()) {
        fout << bestScore;
        fout.close();
    }
}

// TOMBOL UI

struct Button { float x, y, w, h; string label; };
Button btnPlay = {0,0,200,50,"PLAY"};
Button btnExit = {0,0,200,50,"EXIT"};
Button btnRestart = {0,0,170,50,"RESTART"};
Button btnMenu    = {0,0,170,50,"MENU"};

// UTIL GAMBAR
void drawRect(float x, float y, float w, float h, float r, float g, float b) {
    glColor4f(r, g, b, 1.0f);
    glBegin(GL_QUADS);
        glVertex2f(x, y); glVertex2f(x + w, y);
        glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
}

void drawCircle(float cx, float cy, float radius, float r, float g, float b) {
    glColor4f(r, g, b, 1.0f);
    glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= 24; i++) {
            float a = i * 2.0f * 3.14159265f / 24;
            glVertex2f(cx + cosf(a) * radius, cy + sinf(a) * radius);
        }
    glEnd();
}

void drawTriangleShape(float x1,float y1,float x2,float y2,float x3,float y3,float r,float g,float b){
    glColor4f(r,g,b,1.0f);
    glBegin(GL_TRIANGLES);
        glVertex2f(x1,y1); glVertex2f(x2,y2); glVertex2f(x3,y3);
    glEnd();
}

// Gambar poligon bebas dari daftar titik lokal (relatif terhadap cx,cy, opsional rotasi derajat)
void drawPoly(float cx, float cy, const float pts[][2], int n, float r, float g, float b, float alpha=1.0f, float rotDeg=0.0f) {
    float rad = rotDeg * 3.14159265f / 180.0f;
    float cs = cosf(rad), sn = sinf(rad);
    glColor4f(r,g,b,alpha);
    glBegin(GL_POLYGON);
    for (int i=0;i<n;i++){
        float lx = pts[i][0], ly = pts[i][1];
        float rx = lx*cs - ly*sn, ry = lx*sn + ly*cs;
        glVertex2f(cx+rx, cy+ry);
    }
    glEnd();
}

void drawCircleAlpha(float cx, float cy, float radius, float r, float g, float b, float alpha) {
    glColor4f(r, g, b, alpha);
    glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= 24; i++) {
            float a = i * 2.0f * 3.14159265f / 24;
            glVertex2f(cx + cosf(a) * radius, cy + sinf(a) * radius);
        }
    glEnd();
}

// Memutar titik lokal (lx,ly) sejauh sudut yang sama dengan drawPoly, untuk menempatkan
// bagian tambahan (mis. lampu/inti energi) agar ikut berputar bersama bentuk poligonnya
void rotOffset(float lx, float ly, float rotDeg, float &ox, float &oy) {
    float rad = rotDeg * 3.14159265f / 180.0f;
    float cs = cosf(rad), sn = sinf(rad);
    ox = lx*cs - ly*sn;
    oy = lx*sn + ly*cs;
}

void drawText(float x, float y, const string &text, void *font = GLUT_BITMAP_HELVETICA_18,
              float r = 1, float g = 1, float b = 1) {
    glColor4f(r, g, b, 1.0f);
    glRasterPos2f(x, y);
    for (size_t i = 0; i < text.size(); i++) glutBitmapCharacter(font, text[i]);
}

float textWidth(const string &text, void *font = GLUT_BITMAP_HELVETICA_18) {
    float w = 0;
    for (size_t i = 0; i < text.size(); i++) w += glutBitmapWidth(font, text[i]);
    return w;
}

void drawButton(const Button &b, float r, float g, float bcol) {
    // bayangan
    glColor4f(0,0,0,0.25f);
    glBegin(GL_QUADS);
        glVertex2f(b.x+3, b.y-3); glVertex2f(b.x+b.w+3, b.y-3);
        glVertex2f(b.x+b.w+3, b.y+b.h-3); glVertex2f(b.x+3, b.y+b.h-3);
    glEnd();
    // badan tombol (gradasi atas terang -> bawah gelap)
    drawRect(b.x, b.y, b.w, b.h, r*0.6f, g*0.6f, bcol*0.6f);
    drawRect(b.x, b.y+b.h*0.5f, b.w, b.h*0.5f, r, g, bcol);
    drawRect(b.x+2, b.y+b.h-8, b.w-4, 5, r+ (1-r)*0.5f, g+(1-g)*0.5f, bcol+(1-bcol)*0.5f);
    // border
    glColor4f(1,1,1,0.35f);
    glBegin(GL_LINE_LOOP);
        glVertex2f(b.x, b.y); glVertex2f(b.x+b.w, b.y);
        glVertex2f(b.x+b.w, b.y+b.h); glVertex2f(b.x, b.y+b.h);
    glEnd();

    float tw = textWidth(b.label);
    drawText(b.x + (b.w - tw) / 2.0f + 1, b.y + b.h / 2.0f - 8, b.label, GLUT_BITMAP_HELVETICA_18, 0,0,0);
    drawText(b.x + (b.w - tw) / 2.0f, b.y + b.h / 2.0f - 7, b.label);
}

bool insideButton(const Button &b, int mx, int my) {
    return (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h);
}

void layoutButtons() {
    btnPlay.x = WIN_W/2.0f - btnPlay.w/2.0f; btnPlay.y = 300;
    btnExit.x = WIN_W/2.0f - btnExit.w/2.0f; btnExit.y = 230;
    btnRestart.x = WIN_W/2.0f - btnRestart.w - 10; btnRestart.y = 250;
    btnMenu.x    = WIN_W/2.0f + 10;                btnMenu.y = 250;
}

float dist(float x1,float y1,float x2,float y2){
    return sqrtf((x1-x2)*(x1-x2)+(y1-y2)*(y1-y2));
}

// SETUP DUNIA (laut, pulau besar di 4 sudut, islet kecil, kilau air)
void setupTrees() {
    forest.clear();
    islands.clear();
    islets.clear();
    sparkles.clear();

    // --- 4 pulau besar di sudut layar (statis, diam) ---
    float isz = 190.0f;
    float margin = 95.0f;
    float cornerX[4] = { margin, WIN_W-margin, margin, WIN_W-margin };
    float cornerY[4] = { WIN_H-margin, WIN_H-margin, margin, margin };
    for (int k=0;k<4;k++){
        Island is; is.x = cornerX[k]; is.y = cornerY[k]; is.size = isz;
        islands.push_back(is);
    }

    // pohon tersebar di area hijau tiap pulau (statis, tidak bergerak)
    for (size_t k=0;k<islands.size();k++){
        Island &is = islands[k];
        int treeCount = 14;
        for (int i=0;i<treeCount;i++){
            float ang = (float)(rand()%628)/100.0f;
            float rad = (float)(rand()%100)/100.0f * (is.size*0.30f);
            ForestTree t;
            t.x = is.x + cosf(ang)*rad;
            t.y = is.y + sinf(ang)*rad;
            t.layer = 2;
            t.shape = rand()%3;
            t.scale = 0.6f + (rand()%40)/100.0f;
            forest.push_back(t);
        }
    }

    // --- islet kecil tersebar di kanal tengah & dekat pulau (statis) ---
    float isletX[10], isletY[10];
    isletX[0]=WIN_W*0.5f-40; isletY[0]=WIN_H-30;
    isletX[1]=WIN_W*0.32f;   isletY[1]=WIN_H*0.78f;
    isletX[2]=WIN_W*0.62f;   isletY[2]=WIN_H*0.78f;
    isletX[3]=WIN_W*0.28f;   isletY[3]=WIN_H*0.55f;
    isletX[4]=WIN_W*0.68f;   isletY[4]=WIN_H*0.58f;
    isletX[5]=WIN_W*0.34f;   isletY[5]=WIN_H*0.42f;
    isletX[6]=WIN_W*0.66f;   isletY[6]=WIN_H*0.40f;
    isletX[7]=WIN_W*0.30f;   isletY[7]=WIN_H*0.22f;
    isletX[8]=WIN_W*0.66f;   isletY[8]=WIN_H*0.20f;
    isletX[9]=WIN_W*0.5f;    isletY[9]=WIN_H*0.08f;
    for (int i=0;i<10;i++){
        Islet it;
        it.x = isletX[i]; it.y = isletY[i];
        it.size = 22.0f + (rand()%18);
        it.hasTree = (rand()%2==0);
        islets.push_back(it);
        if (it.hasTree) {
            ForestTree t;
            t.x = it.x; t.y = it.y;
            t.layer = 2;
            t.shape = rand()%3;
            t.scale = 0.45f + (rand()%25)/100.0f;
            forest.push_back(t);
        }
    }

    // --- kilau air (sparkle) tersebar acak di lautan ---
    for (int i=0;i<70;i++){
        Sparkle s;
        s.x = (float)(rand()%WIN_W);
        s.y = (float)(rand()%WIN_H);
        s.phase = (float)(rand()%628)/100.0f;
        sparkles.push_back(s);
    }
}

// BAYANGAN AWAN (melayang pelan di atas hutan, kesan kedalaman)
struct CloudShadow { float x, y, scale; };
vector<CloudShadow> clouds;

void setupClouds() {
    clouds.clear();
    for (int i = 0; i < 5; i++) {
        CloudShadow c;
        c.x = rand() % WIN_W;
        c.y = rand() % WIN_H;
        c.scale = 0.7f + (rand()%80)/100.0f;
        clouds.push_back(c);
    }
}

void updateClouds() {
    for (size_t i = 0; i < clouds.size(); i++) {
        clouds[i].x += 0.35f * clouds[i].scale;
        if (clouds[i].x > WIN_W + 90) clouds[i].x = -90;
    }
}

// KILAU AIR LAUT (menggunakan daftar sparkle yang sebelumnya tidak terpakai)
void drawSparkles() {
    for (size_t i=0;i<sparkles.size();i++){
        Sparkle &sp = sparkles[i];
        float tw = 0.5f + 0.5f*sinf(runFrame*0.03f + sp.phase);
        if (tw < 0.35f) continue; // sebagian besar waktu redup/tidak terlihat, berkedip sesekali
        drawCircleAlpha(sp.x, sp.y, 1.6f, 0.85f, 0.95f, 1.0f, (tw-0.35f)*0.9f);
    }
}

void drawCloudShadows() {
    for (size_t i = 0; i < clouds.size(); i++) {
        float x = clouds[i].x, y = clouds[i].y, s = clouds[i].scale;
        drawCircleAlpha(x, y, 55*s, 0,0,0, 0.10f);
        drawCircleAlpha(x+35*s, y-8*s, 40*s, 0,0,0, 0.08f);
        drawCircleAlpha(x-30*s, y-5*s, 35*s, 0,0,0, 0.08f);
    }
}

// GAMBAR DARATAN (pulau besar + islet kecil): pasir pantai, rumput,
// dan bercak tekstur supaya pohon benar-benar berpijak di tanah.
void drawIslands() {
    // pulau besar di 4 sudut
    for (size_t k=0;k<islands.size();k++){
        Island &is = islands[k];
        float s = is.size;
        // bayangan lembut di air sekitar pulau
        drawCircleAlpha(is.x, is.y-6, s*0.62f, 0,0,0, 0.12f);
        // cincin pasir/pantai (dangkal -> lebih terang)
        drawCircle(is.x, is.y, s*0.58f, 0.75f, 0.70f, 0.48f);
        drawCircle(is.x, is.y, s*0.52f, 0.82f, 0.78f, 0.56f);
        // daratan rumput utama, sedikit tak beraturan pakai beberapa lingkaran offset
        drawCircle(is.x, is.y, s*0.46f, 0.22f, 0.46f, 0.22f);
        drawCircle(is.x - s*0.14f, is.y + s*0.10f, s*0.32f, 0.24f, 0.50f, 0.24f);
        drawCircle(is.x + s*0.16f, is.y - s*0.08f, s*0.30f, 0.20f, 0.44f, 0.21f);
        drawCircle(is.x + s*0.05f, is.y + s*0.18f, s*0.26f, 0.26f, 0.52f, 0.26f);
        // bercak tekstur rumput (deterministik berdasarkan indeks, biar tidak "kedip")
        for (int i=0;i<9;i++){
            float ang = (float)k*0.7f + i * (6.2832f/9.0f);
            float rr = s*0.20f + 0.08f*s*sinf(i*1.7f+k);
            float px = is.x + cosf(ang)*rr;
            float py = is.y + sinf(ang)*rr*0.9f;
            drawCircleAlpha(px, py, s*0.09f, 0.18f, 0.40f, 0.18f, 0.55f);
        }
        // sedikit highlight rumput di sisi atas (kesan cahaya datang dari atas)
        drawCircleAlpha(is.x - s*0.06f, is.y + s*0.16f, s*0.30f, 0.35f, 0.62f, 0.30f, 0.35f);
    }

    // islet kecil
    for (size_t i=0;i<islets.size();i++){
        Islet &it = islets[i];
        float s = it.size;
        drawCircleAlpha(it.x, it.y-3, s*0.9f, 0,0,0, 0.10f);
        drawCircle(it.x, it.y, s*0.85f, 0.78f, 0.73f, 0.50f);
        drawCircle(it.x, it.y, s*0.65f, 0.24f, 0.48f, 0.24f);
        drawCircleAlpha(it.x - s*0.15f, it.y + s*0.12f, s*0.30f, 0.34f, 0.60f, 0.30f, 0.35f);
    }
}

// RESET / MULAI WAVE BARU
void startWave(int w) {
    wave = w;
    waveAnnounceTimer = 90;
}

void resetGame() {
    playerX = WIN_W/2.0f; playerY = 100;
    hp = maxHP; invulnTimer = 0; fireCooldown = 0;
    bullets.clear(); enemies.clear(); chests.clear();
    particles.clear(); floatingTexts.clear(); shakeTime=0; shakeMag=0;
    weaponCrates.clear(); weaponTimer=0; weaponSpawnTimer=0; nextWeaponSpawn=500;
    score = 0; chestSpawnTimer = 0;
    nextWaveScore = SCORE_PER_WAVE;
    setupTrees();
    startWave(1);
}

// SPAWN MUSUH (dengan pola sederhana)
void spawnEnemy() {
    Enemy e;
    e.x = 60 + rand() % (WIN_W - 120);
    e.y = WIN_H + 20;
    e.type = (rand() % 4 == 0) ? 1 : 0;   // ~25% UFO, sisanya drone
    e.hp = (e.type == 1) ? 3 : 1;
    e.vy = -(1.0f + wave * 0.08f) - (e.type==1 ? 0.2f : 0.0f);
    e.vx = 0;
    e.phase = (float)(rand() % 628) / 100.0f;
    e.shootTimer = 60 + rand() % 60;
    e.alive = true;
    enemies.push_back(e);
}

// UPDATE LOGIKA GAME
void updateGame() {
    // ----- gerak pemain (segala arah) -----
    float mvx = 0, mvy = 0;
    if (keyUp) mvy += 1;
    if (keyDown) mvy -= 1;
    if (keyLeft) mvx -= 1;
    if (keyRight) mvx += 1;
    if (mvx != 0 || mvy != 0) {
        float len = sqrtf(mvx*mvx+mvy*mvy);
        playerX += (mvx/len) * PLAYER_SPEED;
        playerY += (mvy/len) * PLAYER_SPEED;
    }
    if (playerX < 25) playerX = 25;
    if (playerX > WIN_W-25) playerX = WIN_W-25;
    if (playerY < 25) playerY = 25;
    if (playerY > WIN_H-25) playerY = WIN_H-25;
    if ((mvx != 0 || mvy != 0) && runFrame % 2 == 0) {
        spawnTrail(playerX, playerY-20, 0.55f, 0.75f, 1.0f);
    }

    // ----- tembak -----
    if (fireCooldown > 0) fireCooldown--;
    if (keySpace && fireCooldown == 0) {
        if (weaponTimer > 0) {
            // senjata power-up aktif: tembakan tripel menyebar, lebih cepat
            float speed = 9.5f;
            float angles[3] = { -16.0f, 0.0f, 16.0f };
            for (int a=0;a<3;a++) {
                float rad = angles[a]*3.14159265f/180.0f;
                Bullet b; b.x = playerX; b.y = playerY+18;
                b.vx = sinf(rad)*speed; b.vy = cosf(rad)*speed;
                b.active = true; b.fromPlayer = true; b.powered = true;
                bullets.push_back(b);
            }
            fireCooldown = FIRE_DELAY/2;
            playShootSound();
        } else {
            Bullet b; b.x = playerX; b.y = playerY+18; b.vx = 0; b.vy = 9.0f;
            b.active = true; b.fromPlayer = true; b.powered = false;
            bullets.push_back(b);
            fireCooldown = FIRE_DELAY;
            playShootSound();
        }
    }
    if (invulnTimer > 0) invulnTimer--;
    if (weaponTimer > 0) weaponTimer -= 1.0f;

    // ----- gerak peluru -----
    for (size_t i=0;i<bullets.size();i++){
        bullets[i].x += bullets[i].vx;
        bullets[i].y += bullets[i].vy;
        if (bullets[i].y < -20 || bullets[i].y > WIN_H+20) bullets[i].active=false;
    }
    for (int i=(int)bullets.size()-1;i>=0;i--) if(!bullets[i].active) bullets.erase(bullets.begin()+i);

    // ----- spawn musuh terus-menerus, makin cepat seiring wave naik -----
    int spawnInterval = 45 - wave * 2;
    if (spawnInterval < 16) spawnInterval = 16;
    enemySpawnTimer++;
    if (enemySpawnTimer >= spawnInterval) {
        spawnEnemy();
        enemySpawnTimer = 0;
    }

    // ----- AI & gerak musuh -----
    for (size_t i=0;i<enemies.size();i++){
        Enemy &e = enemies[i];
        if (!e.alive) continue;
        e.y += e.vy;
        if (e.type == 1) {
            // UFO: gerak zig-zag (pola serangan)
            e.phase += 0.06f;
            e.x += sinf(e.phase) * 2.2f;
        }
        if (e.x < 30) e.x = 30;
        if (e.x > WIN_W-30) e.x = WIN_W-30;

        // AI sederhana: menembak, UFO membidik ke arah pemain
        e.shootTimer--;
        if (e.shootTimer <= 0 && e.y > 60 && e.y < WIN_H-40) {
            Bullet b; b.x=e.x; b.y=e.y-10; b.active=true; b.fromPlayer=false;
            if (e.type == 1) {
                float dx = playerX - e.x, dy = playerY - e.y;
                float len = sqrtf(dx*dx+dy*dy); if(len<0.001f) len=0.001f;
                b.vx = (dx/len)*5.5f; b.vy = (dy/len)*5.5f;
            } else {
                b.vx = 0; b.vy = -5.0f;
            }
            bullets.push_back(b);
            e.shootTimer = 90 + rand()%50 - wave*2;
            if (e.shootTimer < 40) e.shootTimer = 40;
        }
        if (e.y < -40) e.alive = false; // lolos ke bawah, hilang
    }
    for (int i=(int)enemies.size()-1;i>=0;i--) if(!enemies[i].alive) enemies.erase(enemies.begin()+i);

    // ----- collision peluru pemain vs musuh -----
    for (size_t i=0;i<bullets.size();i++){
        if (!bullets[i].active || !bullets[i].fromPlayer) continue;
        for (size_t j=0;j<enemies.size();j++){
            if (!enemies[j].alive) continue;
            float r = (enemies[j].type==1)?22:16;
            if (dist(bullets[i].x,bullets[i].y,enemies[j].x,enemies[j].y) < r) {
                bullets[i].active = false;
                enemies[j].hp--;
                if (enemies[j].hp <= 0) {
                    enemies[j].alive = false;
                    int gain = (enemies[j].type==1)?30:10;
                    score += gain;
                    if (score > bestScore) { bestScore = score; saveBestScore(); }
                    spawnExplosion(enemies[j].x, enemies[j].y, 1.0f, 0.6f, 0.15f, (enemies[j].type==1)?18:12);
                    playExplosionSound();
                    char sb[16]; sprintf(sb,"+%d", gain);
                    spawnFloatText(enemies[j].x, enemies[j].y+15, sb, 1.0f, 0.9f, 0.3f);
                }
                break;
            }
        }
    }
    for (int i=(int)bullets.size()-1;i>=0;i--) if(!bullets[i].active) bullets.erase(bullets.begin()+i);
    for (int i=(int)enemies.size()-1;i>=0;i--) if(!enemies[i].alive) enemies.erase(enemies.begin()+i);

    // ----- collision peluru musuh vs pemain -----
    if (invulnTimer == 0) {
        for (size_t i=0;i<bullets.size();i++){
            if (!bullets[i].active || bullets[i].fromPlayer) continue;
            if (dist(bullets[i].x,bullets[i].y,playerX,playerY) < 14) {
                bullets[i].active=false;
                hp--; invulnTimer = 60;
                spawnExplosion(playerX, playerY, 1.0f, 0.3f, 0.2f, 8);
                triggerShake(6.0f, 14.0f);
                if (hp <= 0) { state = GAMEOVER; if(score>bestScore){bestScore=score; saveBestScore();} }
            }
        }
        // tabrakan langsung dengan musuh
        for (size_t j=0;j<enemies.size();j++){
            if (!enemies[j].alive) continue;
            float r = (enemies[j].type==1)?22:16;
            if (dist(playerX,playerY,enemies[j].x,enemies[j].y) < r) {
                enemies[j].alive=false; hp--; invulnTimer=60;
                spawnExplosion(enemies[j].x, enemies[j].y, 1.0f, 0.6f, 0.15f, 14);
                playExplosionSound();
                triggerShake(8.0f, 16.0f);
                if (hp <= 0) { state = GAMEOVER; if(score>bestScore){bestScore=score; saveBestScore();} }
            }
        }
    }
    for (int i=(int)bullets.size()-1;i>=0;i--) if(!bullets[i].active) bullets.erase(bullets.begin()+i);

    // ----- chest (isi ulang HP) -----
    chestSpawnTimer++;
    if (chestSpawnTimer > 500) {
        Chest c; c.x = 60+rand()%(WIN_W-120); c.y = WIN_H+20; c.active=true;
        chests.push_back(c);
        chestSpawnTimer = 0;
    }
    for (size_t i=0;i<chests.size();i++){
        if(!chests[i].active) continue;
        chests[i].y -= 1.4f;
        if (dist(chests[i].x,chests[i].y,playerX,playerY) < 24) {
            chests[i].active=false;
            if (hp < maxHP) { hp++; spawnFloatText(chests[i].x, chests[i].y, "+1 HP", 0.4f, 1.0f, 0.5f); }
            else { score += 20; if (score > bestScore) { bestScore = score; saveBestScore(); } spawnFloatText(chests[i].x, chests[i].y, "+20", 1.0f, 0.9f, 0.3f); }
        }
        if (chests[i].y < -30) chests[i].active=false;
    }
    for (int i=(int)chests.size()-1;i>=0;i--) if(!chests[i].active) chests.erase(chests.begin()+i);

    // ----- crate senjata (power-up tembakan tripel, 10 detik) -----
    weaponSpawnTimer++;
    if (weaponCrates.empty() && weaponSpawnTimer > nextWeaponSpawn) {
        WeaponCrate c; c.x = 60+rand()%(WIN_W-120); c.y = WIN_H+20; c.active=true;
        weaponCrates.push_back(c);
        weaponSpawnTimer = 0;
    }
    for (size_t i=0;i<weaponCrates.size();i++){
        if(!weaponCrates[i].active) continue;
        weaponCrates[i].y -= 1.3f;
        if (dist(weaponCrates[i].x,weaponCrates[i].y,playerX,playerY) < 24) {
            weaponCrates[i].active=false;
            weaponTimer = WEAPON_DURATION;
            weaponSpawnTimer = 0;
            nextWeaponSpawn = WEAPON_RESPAWN_DELAY; // senjata berikutnya baru muncul ~30 detik setelah dipakai
            spawnFloatText(weaponCrates[i].x, weaponCrates[i].y, "SENJATA!", 0.6f, 0.85f, 1.0f);
            spawnExplosion(weaponCrates[i].x, weaponCrates[i].y, 0.5f, 0.8f, 1.0f, 16);
        }
        if (weaponCrates[i].y < -30) weaponCrates[i].active=false;
    }
    for (int i=(int)weaponCrates.size()-1;i>=0;i--) if(!weaponCrates[i].active) weaponCrates.erase(weaponCrates.begin()+i);

    // ----- naik wave setiap kelipatan skor tertentu (misal tiap 200 poin) -----
    if (score >= nextWaveScore) {
        startWave(wave+1);
        nextWaveScore += SCORE_PER_WAVE;
    }
    if (waveAnnounceTimer > 0) waveAnnounceTimer--;

    // ----- update partikel ledakan/trail -----
    for (size_t i=0;i<particles.size();i++){
        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        particles[i].vx *= 0.94f; particles[i].vy *= 0.94f;
        particles[i].life -= 1.0f;
    }
    for (int i=(int)particles.size()-1;i>=0;i--) if (particles[i].life <= 0) particles.erase(particles.begin()+i);

    // ----- update teks melayang -----
    for (size_t i=0;i<floatingTexts.size();i++){
        floatingTexts[i].y += 0.8f;
        floatingTexts[i].life -= 1.0f;
    }
    for (int i=(int)floatingTexts.size()-1;i>=0;i--) if (floatingTexts[i].life<=0) floatingTexts.erase(floatingTexts.begin()+i);

    // ----- peluruhan screen shake -----
    if (shakeTime > 0) shakeTime -= 1.0f; else shakeMag = 0;
}

// GAMBAR OBJEK
void drawBackground() {
    // gradasi vertikal laut: atas lebih terang/kebiruan (langit & cakrawala), bawah lebih dalam & gelap
    int bands = 24;
    for (int i=0;i<bands;i++){
        float t = (float)i/(bands-1);
        float y0 = i * (WIN_H/(float)bands);
        float y1 = (i+1) * (WIN_H/(float)bands);
        float r = 0.03f + 0.05f*t;
        float g = 0.16f + 0.14f*t;
        float b = 0.24f + 0.20f*t;
        drawRect(0, y0, WIN_W, y1-y0+1, r, g, b);
    }

    // vignette di sudut supaya fokus mata ke tengah
    float vg = 0.35f;
    glColor4f(0,0,0,vg);
    glBegin(GL_QUADS); glVertex2f(0,0); glVertex2f(90,0); glVertex2f(0,90); glVertex2f(0,0); glEnd();
    glBegin(GL_TRIANGLES); glVertex2f(0,0); glVertex2f(110,0); glVertex2f(0,110); glEnd();
    glBegin(GL_TRIANGLES); glVertex2f(WIN_W,0); glVertex2f(WIN_W-110,0); glVertex2f(WIN_W,110); glEnd();
    glBegin(GL_TRIANGLES); glVertex2f(0,WIN_H); glVertex2f(110,WIN_H); glVertex2f(0,WIN_H-110); glEnd();
    glBegin(GL_TRIANGLES); glVertex2f(WIN_W,WIN_H); glVertex2f(WIN_W-110,WIN_H); glVertex2f(WIN_W,WIN_H-110); glEnd();
}

void drawTrees() {
    // gambar berurutan dari layer belakang (jauh, gelap, kecil) ke depan (dekat, terang, besar)
    for (int layer = 0; layer < 3; layer++) {
        for (size_t i = 0; i < forest.size(); i++) {
            ForestTree &t = forest[i];
            if (t.layer != layer) continue;

            float s = t.scale;
            // makin jauh (layer kecil) makin gelap & agak kebiruan (kabut kedalaman)
            float depthDim = 0.55f + layer * 0.22f;
            float r = 0.16f * depthDim, g = (0.42f + t.shape*0.05f) * depthDim, b = 0.18f * depthDim;

            // bayangan di tanah, biar pohon terlihat berpijak (bukan melayang)
            drawCircleAlpha(t.x + 3*s, t.y - 17*s, 9*s, 0,0,0, 0.22f);

            // batang
            drawRect(t.x - 3*s, t.y - 16*s, 6*s, 16*s, 0.32f*depthDim, 0.22f*depthDim, 0.12f*depthDim);

            // bentuk daun bervariasi supaya hutan tidak monoton
            if (t.shape == 0) {
                drawCircle(t.x, t.y + 9*s, 15*s, r, g, b);
                drawCircle(t.x - 9*s, t.y + 1*s, 11*s, r, g, b);
                drawCircle(t.x + 9*s, t.y + 1*s, 11*s, r, g, b);
            } else if (t.shape == 1) {
                float pineA[][2] = { {0, 26*s}, {13*s, 4*s}, {-13*s, 4*s} };
                drawPoly(t.x, t.y, pineA, 3, r, g, b, 1.0f, 0.0f);
                float pineB[][2] = { {0, 14*s}, {10*s, -4*s}, {-10*s, -4*s} };
                drawPoly(t.x, t.y, pineB, 3, r, g, b, 1.0f, 0.0f);
            } else {
                drawCircle(t.x, t.y + 6*s, 16*s, r, g, b);
                drawCircle(t.x, t.y + 14*s, 9*s, r*1.15f, g*1.1f, b*1.15f);
            }
        }
    }
}

void drawPlayer() {
    if (invulnTimer>0 && (invulnTimer/4)%2==0) return; // efek kedip saat kebal
    float x = playerX, y = playerY;

    // aura energi saat senjata power-up aktif
    if (weaponTimer > 0) {
        float pulse = 0.35f + 0.15f*sinf(runFrame*0.3f);
        drawCircleAlpha(x, y+2, 30, 0.5f, 0.8f, 1.0f, pulse);
    }

    // --- api mesin (berkedip, di belakang badan pesawat) ---
    float flame = 8.0f + sinf(runFrame*0.9f)*3.0f;
    float flamePts[][2] = { {-4,-16}, {4,-16}, {0,-16-flame} };
    drawPoly(x, y, flamePts, 3, 1.0f, 0.75f, 0.15f, 0.85f);
    float flamePts2[][2] = { {-2.5f,-16}, {2.5f,-16}, {0,-16-flame*0.55f} };
    drawPoly(x, y, flamePts2, 3, 1.0f, 0.95f, 0.5f, 0.95f);

    // --- sayap belakang (lebar, menyapu ke belakang) ---
    float wingL[][2] = { {-5,2}, {-30,-10}, {-22,-16}, {-4,-8} };
    drawPoly(x, y, wingL, 4, 0.75f, 0.80f, 0.85f, 1.0f);
    float wingR[][2] = { {5,2}, {30,-10}, {22,-16}, {4,-8} };
    drawPoly(x, y, wingR, 4, 0.75f, 0.80f, 0.85f, 1.0f);
    // aksen merah di ujung sayap
    drawCircle(x-27, y-12, 2.5f, 0.9f,0.15f,0.15f);
    drawCircle(x+27, y-12, 2.5f, 0.15f,0.7f,0.2f);

    // --- badan utama pesawat (hidung lancip, melebar, ekor meruncing) ---
    float body[][2] = {
        {0, 26},     // hidung
        {6, 10}, {7,-4}, {5,-14}, {2,-18},
        {-2,-18}, {-5,-14}, {-7,-4}, {-6,10}
    };
    drawPoly(x, y, body, 9, 0.20f, 0.45f, 0.85f, 1.0f);
    // garis tengah lebih terang (highlight)
    float bodyHi[][2] = { {0,22},{2.4f,8},{2.0f,-14},{-2.0f,-14},{-2.4f,8} };
    drawPoly(x, y, bodyHi, 5, 0.45f, 0.68f, 0.98f, 1.0f);

    // --- sirip ekor kecil ---
    float finL[][2] = { {-5,-10}, {-11,-18}, {-4,-16} };
    drawPoly(x, y, finL, 3, 0.15f,0.30f,0.6f, 1.0f);
    float finR[][2] = { {5,-10}, {11,-18}, {4,-16} };
    drawPoly(x, y, finR, 3, 0.15f,0.30f,0.6f, 1.0f);

    // --- kokpit (kaca) ---
    drawCircleAlpha(x, y+9, 4.2f, 0.65f, 0.9f, 1.0f, 0.9f);
    drawCircleAlpha(x, y+9, 2.2f, 0.85f, 0.98f, 1.0f, 0.9f);
}

void drawEnemies() {
    for (size_t i=0;i<enemies.size();i++){
        Enemy &e = enemies[i];
        float x = e.x, y = e.y;
        if (e.type == 0) {
            // --- drone: pesawat penyerbu ramping, menghadap ke bawah (mendekati pemain) ---
            float flame = 5.0f + sinf((runFrame+i*7)*0.8f)*2.0f;
            float flameP[][2] = { {-2.5f,10}, {2.5f,10}, {0,10+flame} };
            drawPoly(x, y, flameP, 3, 1.0f,0.6f,0.2f, 0.8f);

            float wingL[][2] = { {-3,2}, {-20,8}, {-14,11}, {-3,6} };
            drawPoly(x, y, wingL, 4, 0.55f,0.18f,0.18f, 1.0f);
            float wingR[][2] = { {3,2}, {20,8}, {14,11}, {3,6} };
            drawPoly(x, y, wingR, 4, 0.55f,0.18f,0.18f, 1.0f);

            float body[][2] = { {0,-15}, {5,-2}, {4,9}, {0,13}, {-4,9}, {-5,-2} };
            drawPoly(x, y, body, 6, 0.75f,0.25f,0.25f, 1.0f);
            float bodyHi[][2] = { {0,-11}, {2,-1}, {0,9}, {-2,-1} };
            drawPoly(x, y, bodyHi, 4, 0.92f,0.45f,0.42f, 1.0f);

            drawCircleAlpha(x, y-4, 3.0f, 1.0f, 0.95f, 0.5f, 0.9f);
        } else {
            // --- UFO: piring terbang bertingkat dengan lampu berkedip ---
            drawCircleAlpha(x, y-3, 24, 0.05f,0.05f,0.05f, 0.18f); // bayangan lembut
            drawCircle(x, y, 21, 0.35f,0.55f,0.28f);
            drawCircle(x, y+3, 21, 0.55f,0.80f,0.35f);
            drawCircle(x, y+3, 20, 0.62f,0.88f,0.42f);
            drawRect(x-23, y-3, 46, 6, 0.30f,0.48f,0.20f);

            // lampu berputar di sekeliling badan
            for (int k=0;k<6;k++){
                float ang = e.phase*2.0f + k * (3.14159265f/3.0f);
                float lx = x + cosf(ang)*20.0f;
                float ly = y - 1 + sinf(ang)*4.0f;
                float on = (sinf(ang)>0)?1.0f:0.3f;
                drawCircle(lx, ly, 2.3f, 1.0f*on, 0.9f*on, 0.2f);
            }
            // kubah kokpit atas
            drawCircleAlpha(x, y+9, 11, 0.75f,0.95f,0.85f, 0.85f);
            drawCircleAlpha(x, y+9, 6, 0.9f,1.0f,0.95f, 0.85f);
        }
    }
}

void drawParticles() {
    for (size_t i=0;i<particles.size();i++){
        Particle &p = particles[i];
        float a = p.life / p.maxLife;
        drawCircleAlpha(p.x, p.y, 2.0f + (1.0f-a)*3.0f, p.r, p.g, p.b, a*0.9f);
    }
}

void drawFloatingTexts() {
    for (size_t i=0;i<floatingTexts.size();i++){
        FloatingText &f = floatingTexts[i];
        float a = f.life / f.maxLife;
        glColor4f(f.r,f.g,f.b,a);
        glRasterPos2f(f.x - textWidth(f.text)/2.0f, f.y);
        for (size_t c=0;c<f.text.size();c++) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, f.text[c]);
    }
}

void drawBullets() {
    for (size_t i=0;i<bullets.size();i++){
        float x = bullets[i].x, y = bullets[i].y;
        if (bullets[i].fromPlayer) {
            if (bullets[i].powered) {
                drawCircleAlpha(x, y, 8.0f, 0.5f, 0.8f, 1.0f, 0.45f);   // glow energi
                drawRect(x-2.4f, y-8, 4.8f, 16, 0.4f, 0.7f, 1.0f);
                drawRect(x-1.1f, y-8, 2.2f, 16, 0.85f, 0.95f, 1.0f);
            } else {
                drawCircleAlpha(x, y, 7.0f, 1.0f, 0.9f, 0.3f, 0.35f);   // glow luar
                drawRect(x-2.2f, y-7, 4.4f, 14, 1.0f, 0.85f, 0.15f);
                drawRect(x-1.0f, y-7, 2.0f, 14, 1.0f, 1.0f, 0.75f);      // inti terang
            }
        } else {
            drawCircleAlpha(x, y, 7.0f, 1.0f, 0.25f, 0.2f, 0.35f);
            drawRect(x-2.2f, y-7, 4.4f, 14, 0.95f, 0.15f, 0.15f);
            drawRect(x-1.0f, y-7, 2.0f, 14, 1.0f, 0.55f, 0.5f);
        }
    }
}

void drawWeaponCrates() {
    for (size_t i=0;i<weaponCrates.size();i++){
        if(!weaponCrates[i].active) continue;
        float x = weaponCrates[i].x;
        float bob = sinf(runFrame*0.08f + i*2.1f) * 3.0f;   // melayang naik-turun pelan
        float y = weaponCrates[i].y + bob;
        float rotDeg = sinf(runFrame*0.04f + i*1.7f) * 12.0f; // sedikit berputar goyang

        // aura cahaya di bawah senjata
        float glow = 0.5f + 0.3f*sinf(runFrame*0.18f + i);
        drawCircleAlpha(x, y, 22, 0.5f, 0.8f, 1.0f, glow*0.4f);

        // --- gagang pistol (menyudut ke belakang-bawah) ---
        float grip[][2] = { {-9,-2}, {-15,-15}, {-10,-17}, {-5,-4} };
        drawPoly(x, y, grip, 4, 0.16f,0.16f,0.20f, 1.0f, rotDeg);

        // --- badan utama pistol ---
        float body[][2] = { {-11,5}, {7,5}, {9,-4}, {-11,-4} };
        drawPoly(x, y, body, 4, 0.30f,0.33f,0.38f, 1.0f, rotDeg);
        // aksen terang di atas badan
        float bodyHi[][2] = { {-11,4.3f}, {7,4.3f}, {7,1.8f}, {-11,1.8f} };
        drawPoly(x, y, bodyHi, 4, 0.55f,0.60f,0.68f, 1.0f, rotDeg);

        // --- laras panjang ke depan ---
        float barrel[][2] = { {7,2.6f}, {23,2.6f}, {23,-1.6f}, {7,-1.6f} };
        drawPoly(x, y, barrel, 4, 0.60f,0.65f,0.72f, 1.0f, rotDeg);
        float barrelHi[][2] = { {7,2.2f}, {23,2.2f}, {23,1.0f}, {7,1.0f} };
        drawPoly(x, y, barrelHi, 4, 0.85f,0.90f,0.98f, 1.0f, rotDeg);

        // --- pelatuk kecil ---
        float trig[][2] = { {-4,-4}, {-1,-4}, {-2,-9} };
        drawPoly(x, y, trig, 3, 0.16f,0.16f,0.20f, 1.0f, rotDeg);

        // --- inti energi berkedip di badan senjata ---
        float ox, oy;
        rotOffset(-2, 0.5f, rotDeg, ox, oy);
        drawCircleAlpha(x+ox, y+oy, 3.2f, 0.45f, 0.85f, 1.0f, 0.95f);
        drawCircleAlpha(x+ox, y+oy, 1.6f, 0.9f, 0.98f, 1.0f, 0.95f);

        // --- ujung laras menyala (siap tembak) ---
        rotOffset(23, 0.5f, rotDeg, ox, oy);
        drawCircleAlpha(x+ox, y+oy, 3.4f, 0.55f, 0.9f, 1.0f, 0.85f);
        drawCircleAlpha(x+ox, y+oy, 1.6f, 0.9f, 1.0f, 1.0f, 0.9f);
    }
}

void drawChests() {
    for (size_t i=0;i<chests.size();i++){
        if(!chests[i].active) continue;
        float x = chests[i].x, y = chests[i].y;
        float glow = 0.4f + 0.25f*sinf(runFrame*0.15f + i);
        drawCircleAlpha(x, y, 22, 1.0f, 0.85f, 0.3f, glow*0.35f);

        drawRect(x-15, y-13, 30, 26, 0.35f,0.20f,0.08f);          // bayangan dasar
        drawRect(x-14, y-12, 28, 24, 0.55f,0.35f,0.15f);          // badan peti kayu
        // garis kayu vertikal
        for (int k=-1;k<=1;k++) drawRect(x-1+k*9, y-12, 2, 24, 0.45f,0.28f,0.12f);
        drawRect(x-14, y+2, 28, 5, 0.75f,0.6f,0.25f);             // tutup
        drawRect(x-14, y-1, 28, 3, 0.30f,0.22f,0.10f);            // celah tutup
        // bingkai logam & paku sudut
        drawRect(x-14, y-13, 28, 3, 0.75f,0.75f,0.78f);
        drawRect(x-14, y+10, 28, 3, 0.75f,0.75f,0.78f);
        drawCircle(x-11, y-11, 1.4f, 0.9f,0.9f,0.9f);
        drawCircle(x+11, y-11, 1.4f, 0.9f,0.9f,0.9f);
        drawCircle(x-11, y+12, 1.4f, 0.9f,0.9f,0.9f);
        drawCircle(x+11, y+12, 1.4f, 0.9f,0.9f,0.9f);
        // gembok kecil
        drawRect(x-3, y-3, 6, 6, 0.85f,0.7f,0.25f);
        drawText(x-4, y-6, "+", GLUT_BITMAP_HELVETICA_18, 1,1,1);
    }
}

void drawHUD() {
    char buf[64];

    // HP sebagai ikon hati dengan outline & kilau (tanpa panel kotak di belakang)
    for (int i=0;i<maxHP;i++){
        float hx = 24 + i*26;
        bool filled = (i < hp);
        drawCircle(hx, WIN_H-20, 9, 0,0,0);
        drawCircle(hx, WIN_H-20, 7.5f, filled?1.0f:0.22f, 0.15f, 0.18f);
        if (filled) drawCircleAlpha(hx-2.3f, WIN_H-22.3f, 2.2f, 1,1,1,0.55f);
    }
    sprintf(buf, "Score: %d", score);
    drawText(20, WIN_H-45, buf);

    sprintf(buf, "Best: %d", bestScore);
    drawText(20, WIN_H-66, buf, GLUT_BITMAP_HELVETICA_18, 1.0f, 0.84f, 0.2f);

    // wave (kanan-atas, tanpa panel kotak)
    sprintf(buf, "Wave %d", wave);
    float ww = textWidth(buf);
    drawText(WIN_W-ww-10, WIN_H-25, buf, GLUT_BITMAP_HELVETICA_18, 1.0f,0.85f,0.3f);

    // sisa skor menuju wave berikutnya
    int need = nextWaveScore - score;
    if (need < 0) need = 0;
    sprintf(buf, "Next wave: %d", need);
    float nw = textWidth(buf);
    drawText(WIN_W-nw-10, WIN_H-45, buf, GLUT_BITMAP_HELVETICA_18, 0.8f,0.9f,0.95f);

    // indikator senjata power-up aktif (tanpa panel kotak di belakang)
    if (weaponTimer > 0) {
        int secs = (int)(weaponTimer / 62.5f) + 1;
        char wb[32]; sprintf(wb, "SENJATA %ds", secs);
        float bw = textWidth(wb);
        float bx = WIN_W/2.0f - bw/2.0f;
        // bar sisa waktu
        float ratio = weaponTimer / WEAPON_DURATION;
        drawRect(bx, WIN_H-28, bw*ratio, 4, 0.5f,0.85f,1.0f);
        drawText(bx, WIN_H-24, wb, GLUT_BITMAP_HELVETICA_18, 0.6f,0.9f,1.0f);
    }

    if (waveAnnounceTimer > 0) {
        char wbuf[32]; sprintf(wbuf, "WAVE %d", wave);
        string s(wbuf);
        float tw = textWidth(s, GLUT_BITMAP_TIMES_ROMAN_24);
        float fade = (waveAnnounceTimer > 20) ? 1.0f : waveAnnounceTimer/20.0f;
        drawText(WIN_W/2.0f - tw/2.0f+1, WIN_H/2.0f+39, s, GLUT_BITMAP_TIMES_ROMAN_24, 0,0,0);
        glColor4f(1,1,0.3f,fade);
        glRasterPos2f(WIN_W/2.0f - tw/2.0f, WIN_H/2.0f+40);
        for (size_t c=0;c<s.size();c++) glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, s[c]);
    }
}

// LAYAR
void drawMenuScreen() {
    drawBackground();
    drawSparkles();
    drawIslands();
    drawTrees();
    drawCloudShadows();

    // pratinjau pesawat pemain melayang pelan di tengah bawah
    float oldX = playerX, oldY = playerY;
    playerX = WIN_W/2.0f; playerY = 130 + sinf(runFrame*0.05f)*8.0f;
    drawPlayer();
    playerX = oldX; playerY = oldY;

    string title = "TOP-DOWN SHOOTER";
    float tw = textWidth(title, GLUT_BITMAP_TIMES_ROMAN_24);
    drawText(WIN_W/2.0f - tw/2.0f+2, 418, title, GLUT_BITMAP_TIMES_ROMAN_24, 0,0,0);
    drawText(WIN_W/2.0f - tw/2.0f, 420, title, GLUT_BITMAP_TIMES_ROMAN_24, 0.55f,0.85f,1.0f);

    string sub = "WASD/Panah = gerak   |   SPASI = tembak";
    drawText(WIN_W/2.0f - textWidth(sub)/2.0f, 380, sub, GLUT_BITMAP_HELVETICA_18, 0.85f,0.85f,0.85f);

    char bbuf[32]; sprintf(bbuf, "Best Score: %d", bestScore);
    string bs(bbuf);
    drawText(WIN_W/2.0f - textWidth(bs)/2.0f, 355, bs, GLUT_BITMAP_HELVETICA_18, 1.0f, 0.84f, 0.2f);

    drawButton(btnPlay, 0.15f,0.65f,0.15f);
    drawButton(btnExit, 0.75f,0.15f,0.15f);
}

void drawGameWorld() {
    drawBackground();
    drawSparkles();
    drawIslands();
    drawTrees();
    drawCloudShadows();
    drawChests();
    drawWeaponCrates();
    drawEnemies();
    drawBullets();
    drawParticles();
    drawFloatingTexts();
    drawPlayer();
}

void drawGameScreen() {
    float sx=0, sy=0;
    if (shakeTime > 0) { sx = (rand()%200-100)/100.0f * shakeMag; sy = (rand()%200-100)/100.0f * shakeMag; }
    glPushMatrix();
        glTranslatef(sx, sy, 0);
        drawGameWorld();
    glPopMatrix();
    drawHUD();
}

void drawGameOverScreen() {
    drawGameWorld();

    glColor4f(0,0,0,0.55f);
    glBegin(GL_QUADS);
        glVertex2f(0,0); glVertex2f(WIN_W,0); glVertex2f(WIN_W,WIN_H); glVertex2f(0,WIN_H);
    glEnd();

    // panel kartu di tengah (dipertahankan sebagai bingkai dialog, bukan "kotak" HUD)
    float pw=340, ph=260, px=WIN_W/2.0f-pw/2.0f, py=WIN_H/2.0f-ph/2.0f+10;
    glColor4f(0,0,0,0.35f);
    glBegin(GL_QUADS); glVertex2f(px+5,py-5); glVertex2f(px+pw+5,py-5); glVertex2f(px+pw+5,py+ph-5); glVertex2f(px+5,py+ph-5); glEnd();
    drawRect(px, py, pw, ph, 0.10f,0.12f,0.16f);
    glColor4f(1,0.25f,0.25f,0.6f);
    glBegin(GL_LINE_LOOP); glVertex2f(px,py); glVertex2f(px+pw,py); glVertex2f(px+pw,py+ph); glVertex2f(px,py+ph); glEnd();

    string title = "GAME OVER";
    float tw = textWidth(title, GLUT_BITMAP_TIMES_ROMAN_24);
    drawText(WIN_W/2.0f - tw/2.0f+2, py+ph-48, title, GLUT_BITMAP_TIMES_ROMAN_24, 0,0,0);
    drawText(WIN_W/2.0f - tw/2.0f, py+ph-46, title, GLUT_BITMAP_TIMES_ROMAN_24, 1,0.3f,0.3f);

    char buf[64];
    sprintf(buf,"Score: %d", score); string s1(buf);
    drawText(WIN_W/2.0f - textWidth(s1)/2.0f, py+ph-95, s1);
    sprintf(buf,"Best: %d", bestScore); string s2(buf);
    drawText(WIN_W/2.0f - textWidth(s2)/2.0f, py+ph-125, s2, GLUT_BITMAP_HELVETICA_18,1,0.84f,0);
    sprintf(buf,"Wave dicapai: %d", wave); string s3(buf);
    drawText(WIN_W/2.0f - textWidth(s3)/2.0f, py+ph-155, s3);

    btnRestart.x = WIN_W/2.0f - btnRestart.w - 10; btnRestart.y = py+20;
    btnMenu.x    = WIN_W/2.0f + 10;                btnMenu.y = py+20;
    drawButton(btnRestart, 0.15f,0.65f,0.15f);
    drawButton(btnMenu, 0.15f,0.35f,0.75f);
}

// CALLBACKS
void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    switch(state) {
        case MENU: drawMenuScreen(); break;
        case PLAYING: drawGameScreen(); break;
        case GAMEOVER: drawGameOverScreen(); break;
    }
    glutSwapBuffers();
}

void reshape(int w, int h) {
    if (h==0) h=1;
    WIN_W = w; WIN_H = h;
    glViewport(0,0,w,h);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluOrtho2D(0,WIN_W,0,WIN_H);
    glMatrixMode(GL_MODELVIEW);
    layoutButtons();
}

void timer(int value) {
    updateClouds();
    if (state == PLAYING) updateGame();
    runFrame++;
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

void keyDownFunc(unsigned char key, int x, int y) {
    if (key=='w'||key=='W') keyUp=true;
    if (key=='s'||key=='S') keyDown=true;
    if (key=='a'||key=='A') keyLeft=true;
    if (key=='d'||key=='D') keyRight=true;
    if (key==' ') keySpace=true;
    if (key==27) exit(0);
}
void keyUpFunc(unsigned char key, int x, int y) {
    if (key=='w'||key=='W') keyUp=false;
    if (key=='s'||key=='S') keyDown=false;
    if (key=='a'||key=='A') keyLeft=false;
    if (key=='d'||key=='D') keyRight=false;
    if (key==' ') keySpace=false;
}
void specialDownFunc(int key,int x,int y){
    if (key==GLUT_KEY_UP) keyUp=true;
    if (key==GLUT_KEY_DOWN) keyDown=true;
    if (key==GLUT_KEY_LEFT) keyLeft=true;
    if (key==GLUT_KEY_RIGHT) keyRight=true;
}
void specialUpFunc(int key,int x,int y){
    if (key==GLUT_KEY_UP) keyUp=false;
    if (key==GLUT_KEY_DOWN) keyDown=false;
    if (key==GLUT_KEY_LEFT) keyLeft=false;
    if (key==GLUT_KEY_RIGHT) keyRight=false;
}

void mouseClick(int button, int buttonState, int mx, int my) {
    if (button != GLUT_LEFT_BUTTON || buttonState != GLUT_DOWN) return;
    int gy = WIN_H - my;
    if (state == MENU) {
        if (insideButton(btnPlay, mx, gy)) { resetGame(); state = PLAYING; }
        else if (insideButton(btnExit, mx, gy)) exit(0);
    } else if (state == GAMEOVER) {
        if (insideButton(btnRestart, mx, gy)) { resetGame(); state = PLAYING; }
        else if (insideButton(btnMenu, mx, gy)) state = MENU;
    }
}

// INIT & MAIN
void init() {
    glClearColor(0.06f,0.22f,0.16f,1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    layoutButtons();
    srand((unsigned int)time(NULL));
    setupTrees();
    setupClouds();
    loadBestScore();
    initSounds();
}

int main(int argc, char **argv) {
    glutInit(&argc, argv); 
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(WIN_W, WIN_H);
    glutInitWindowPosition(80,60);
    glutCreateWindow("Top-Down Shooter - OpenGL/FreeGLUT");

    init();
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyDownFunc);
    glutKeyboardUpFunc(keyUpFunc);
    glutSpecialFunc(specialDownFunc);
    glutSpecialUpFunc(specialUpFunc);
    glutMouseFunc(mouseClick);
    glutTimerFunc(16, timer, 0);

    glutMainLoop();
    return 0;
}
