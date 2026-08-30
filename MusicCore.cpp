#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "default_bg.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
#define NUM_BANDS 6
#define CONFIG_FILE "musiccore_config.txt"
static const double BAND_FREQS[NUM_BANDS] = { 60, 150, 400, 1000, 2400, 8000 };
static const char* BAND_LABELS[NUM_BANDS] = { "60Hz", "150Hz", "400Hz", "1kHz", "2.4kHz", "8kHz" };
static std::wstring cvw(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static std::string cvu(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::string gfn(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}
static std::string sxt(const std::string& name) {
    size_t dot = name.find_last_of('.');
    return (dot == std::string::npos) ? name : name.substr(0, dot);
}
static bool chk(const std::string& name) {
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    return ext == ".mp3" || ext == ".wav" || ext == ".flac";
}
static bool fex(const std::string& pathUtf8) {
    std::wstring w = cvw(pathUtf8);
    DWORD attr = GetFileAttributesW(w.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
struct AudioEngine {
    ma_engine engine{};
    ma_peak_node eq[NUM_BANDS]{};
    double eqGains[NUM_BANDS] = { 0,0,0,0,0,0 };
    ma_sound sound{};
    bool soundLoaded = false;
    float volume = 0.8f;
    void Init() {
        ma_engine_init(nullptr, &engine);
        ma_uint32 ch = ma_engine_get_channels(&engine);
        ma_uint32 sr = ma_engine_get_sample_rate(&engine);
        ma_node_graph* graph = ma_engine_get_node_graph(&engine);
        for (int i = 0; i < NUM_BANDS; i++) {
            ma_peak_node_config cfg = ma_peak_node_config_init(ch, sr, 0.0, 1.0, BAND_FREQS[i]);
            ma_peak_node_init(graph, &cfg, nullptr, &eq[i]);
        }
        for (int i = 0; i < NUM_BANDS - 1; i++)
            ma_node_attach_output_bus((ma_node*)&eq[i], 0, (ma_node*)&eq[i + 1], 0);
        ma_node_attach_output_bus((ma_node*)&eq[NUM_BANDS - 1], 0, ma_node_graph_get_endpoint(graph), 0);
    }
    void SetGain(int band, double db) {
        eqGains[band] = db;
        ma_peak_node_config cfg = ma_peak_node_config_init(ma_engine_get_channels(&engine),
                                                            ma_engine_get_sample_rate(&engine), db, 1.0, BAND_FREQS[band]);
        ma_peak_node_reinit(&cfg.peak, &eq[band]);
    }
    bool Load(const std::string& pathUtf8) {
        if (soundLoaded) { ma_sound_uninit(&sound); soundLoaded = false; }
        std::wstring pathW = cvw(pathUtf8);
        if (ma_sound_init_from_file_w(&engine, pathW.c_str(), 0, nullptr, nullptr, &sound) != MA_SUCCESS)
            return false;
        soundLoaded = true;
        ma_node_attach_output_bus((ma_node*)&sound, 0, (ma_node*)&eq[0], 0);
        ma_sound_set_volume(&sound, volume);
        ma_sound_start(&sound);
        return true;
    }
    void TogglePause() {
        if (!soundLoaded) return;
        if (ma_sound_is_playing(&sound)) ma_sound_stop(&sound);
        else ma_sound_start(&sound);
    }
    void Stop() {
        if (!soundLoaded) return;
        ma_sound_stop(&sound);
        ma_sound_seek_to_pcm_frame(&sound, 0);
    }
    void Shutdown() {
        if (soundLoaded) ma_sound_uninit(&sound);
        for (int i = 0; i < NUM_BANDS; i++) ma_peak_node_uninit(&eq[i], nullptr);
        ma_engine_uninit(&engine);
    }
};
struct Background {
    unsigned char* orig = nullptr;
    unsigned char* blurred = nullptr;
    int* delays = nullptr;
    int w = 0, h = 0, frameCount = 0, curFrame = 0, elapsedMs = 0;
    int blurRadius = 0;
    std::string path;
    bool isDefault = true;
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    void Free() {
        if (orig) { free(orig); orig = nullptr; }
        if (blurred) { free(blurred); blurred = nullptr; }
        if (delays) { free(delays); delays = nullptr; }
        frameCount = 0; curFrame = 0; elapsedMs = 0;
    }
    static void BoxBlurFrame(const unsigned char* src, unsigned char* dst, int w, int h, int radius) {
        if (radius <= 0) { memcpy(dst, src, (size_t)w * h * 4); return; }
        std::vector<unsigned char> tmp((size_t)w * h * 4);
        for (int y = 0; y < h; y++) {
            const unsigned char* row = src + (size_t)y * w * 4;
            unsigned char* orow = tmp.data() + (size_t)y * w * 4;
            for (int x = 0; x < w; x++) {
                int sum[4] = { 0,0,0,0 }, count = 0;
                for (int dx = -radius; dx <= radius; dx++) {
                    int sx = x + dx; if (sx < 0 || sx >= w) continue;
                    const unsigned char* p = row + sx * 4;
                    sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; sum[3] += p[3]; count++;
                }
                orow[x*4+0]=(unsigned char)(sum[0]/count); orow[x*4+1]=(unsigned char)(sum[1]/count);
                orow[x*4+2]=(unsigned char)(sum[2]/count); orow[x*4+3]=(unsigned char)(sum[3]/count);
            }
        }
        for (int x = 0; x < w; x++) {
            for (int y = 0; y < h; y++) {
                int sum[4] = { 0,0,0,0 }, count = 0;
                for (int dy = -radius; dy <= radius; dy++) {
                    int sy = y + dy; if (sy < 0 || sy >= h) continue;
                    const unsigned char* p = tmp.data() + ((size_t)sy * w + x) * 4;
                    sum[0] += p[0]; sum[1] += p[1]; sum[2] += p[2]; sum[3] += p[3]; count++;
                }
                unsigned char* d = dst + ((size_t)y * w + x) * 4;
                d[0]=(unsigned char)(sum[0]/count); d[1]=(unsigned char)(sum[1]/count);
                d[2]=(unsigned char)(sum[2]/count); d[3]=(unsigned char)(sum[3]/count);
            }
        }
    }
    void ApplyBlur() {
        if (!orig) return;
        size_t frameSize = (size_t)w * h * 4;
        for (int f = 0; f < frameCount; f++)
            BoxBlurFrame(orig + f * frameSize, blurred + f * frameSize, w, h, blurRadius);
        UploadCurrentFrame();
    }
    void ApplyBlurToCurrentFrame() {
        if (!orig || !blurred) return;
        size_t frameSize = (size_t)w * h * 4;
        BoxBlurFrame(orig + curFrame * frameSize, blurred + curFrame * frameSize, w, h, blurRadius);
        UploadCurrentFrame();
    }
    void SetupLoaded(unsigned char* rgba, int* newDelays, int nw, int nh, int nFrames, bool isDef) {
        Free();
        size_t total = (size_t)nw * nh * nFrames * 4;
        orig = (unsigned char*)malloc(total);
        blurred = (unsigned char*)malloc(total);
        memcpy(orig, rgba, total);
        stbi_image_free(rgba);
        delays = newDelays;
        w = nw; h = nh; frameCount = nFrames; curFrame = 0; elapsedMs = 0;
        isDefault = isDef;
        EnsureTexture();
        ApplyBlur();
    }
    void LoadFromMemory(const unsigned char* data, long size, bool isGif) {
        int nw, nh, comp;
        if (isGif) {
            int z; int* delaysArr = nullptr;
            unsigned char* frames = stbi_load_gif_from_memory(data, size, &delaysArr, &nw, &nh, &z, &comp, 4);
            if (frames) SetupLoaded(frames, delaysArr, nw, nh, z, true);
        } else {
            unsigned char* img = stbi_load_from_memory(data, size, &nw, &nh, &comp, 4);
            if (img) {
                int* delaysArr = (int*)malloc(sizeof(int)); delaysArr[0] = 1000;
                SetupLoaded(img, delaysArr, nw, nh, 1, true);
            }
        }
    }
    bool LoadFromFile(const std::string& pUtf8) {
        std::wstring pW = cvw(pUtf8);
        FILE* f = _wfopen(pW.c_str(), L"rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
        std::vector<unsigned char> buf(size);
        fread(buf.data(), 1, size, f);
        fclose(f);
        size_t dot = pUtf8.find_last_of('.');
        std::string ext = (dot == std::string::npos) ? "" : pUtf8.substr(dot);
        for (auto& c : ext) c = (char)tolower((unsigned char)c);
        bool isGif = (ext == ".gif");
        int nw, nh, comp;
        if (isGif) {
            int z; int* delaysArr = nullptr;
            unsigned char* frames = stbi_load_gif_from_memory(buf.data(), (int)size, &delaysArr, &nw, &nh, &z, &comp, 4);
            if (!frames) return false;
            SetupLoaded(frames, delaysArr, nw, nh, z, false);
        } else {
            unsigned char* img = stbi_load_from_memory(buf.data(), (int)size, &nw, &nh, &comp, 4);
            if (!img) return false;
            int* delaysArr = (int*)malloc(sizeof(int)); delaysArr[0] = 1000;
            SetupLoaded(img, delaysArr, nw, nh, 1, false);
        }
        path = pUtf8;
        return true;
    }
    void EnsureTexture() {
        if (srv) { srv->Release(); srv = nullptr; }
        if (tex) { tex->Release(); tex = nullptr; }
        if (!device || w == 0 || h == 0) return;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        device->CreateTexture2D(&desc, nullptr, &tex);
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(tex, &srvDesc, &srv);
    }
    void UploadCurrentFrame() {
        if (!tex || !ctx || !blurred) return;
        size_t frameSize = (size_t)w * h * 4;
        ctx->UpdateSubresource(tex, 0, nullptr, blurred + curFrame * frameSize, w * 4, 0);
    }
    void Tick(int dtMs) {
        if (frameCount <= 1) return;
        elapsedMs += dtMs;
        int delay = delays[curFrame]; if (delay <= 0) delay = 100;
        if (elapsedMs >= delay) {
            elapsedMs = 0;
            curFrame = (curFrame + 1) % frameCount;
            UploadCurrentFrame();
        }
    }
};
struct App {
    std::vector<std::string> playlist;
    int currentIndex = -1;
    AudioEngine audio;
    Background bg;
    char status[256] = "Pret.";
    float seekPreview = 0.0f;
    bool seeking = false;
} g_app;
static ImFont* g_f1 = nullptr;
static ImFont* g_f2 = nullptr;
static void SaveConfig() {
    FILE* f = fopen(CONFIG_FILE, "w");
    if (!f) return;
    fprintf(f, "BG=%s\n", g_app.bg.isDefault ? "" : g_app.bg.path.c_str());
    fprintf(f, "BLUR=%d\n", g_app.bg.blurRadius);
    for (auto& p : g_app.playlist) fprintf(f, "%s\n", p.c_str());
    fclose(f);
}
static void AddPathIfAudio(const std::string& p) {
    if (chk(p)) g_app.playlist.push_back(p);
}
static void ScanFolder(const std::string& folderUtf8) {
    std::wstring folderW = cvw(folderUtf8);
    std::wstring pattern = folderW + L"\\*.*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        snprintf(g_app.status, sizeof(g_app.status), "Impossible de lire ce dossier.");
        return;
    }
    int added = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::string full = folderUtf8 + "\\" + cvu(fd.cFileName);
            if (chk(full)) { g_app.playlist.push_back(full); added++; }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    snprintf(g_app.status, sizeof(g_app.status), "%d morceau(x) ajoute(s) depuis le dossier", added);
    SaveConfig();
}
static void LoadConfig() {
    FILE* f = fopen(CONFIG_FILE, "r");
    if (!f) { g_app.bg.LoadFromMemory(DEFAULT_BG_GIF, (long)DEFAULT_BG_GIF_LEN, true); return; }
    char line[1024];
    bool gotBg = false;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (len == 0) continue;
        if (strncmp(line, "BG=", 3) == 0) {
            gotBg = true;
            if (strlen(line) > 3 && fex(line + 3)) g_app.bg.LoadFromFile(line + 3);
            else g_app.bg.LoadFromMemory(DEFAULT_BG_GIF, (long)DEFAULT_BG_GIF_LEN, true);
        } else if (strncmp(line, "BLUR=", 5) == 0) {
            g_app.bg.blurRadius = atoi(line + 5);
        } else if (fex(line)) {
            AddPathIfAudio(line);
        }
    }
    fclose(f);
    if (!gotBg) g_app.bg.LoadFromMemory(DEFAULT_BG_GIF, (long)DEFAULT_BG_GIF_LEN, true);
    if (g_app.bg.blurRadius > 0) g_app.bg.ApplyBlur();
}
static void PlayIndex(int idx) {
    if (idx < 0 || idx >= (int)g_app.playlist.size()) return;
    if (g_app.audio.Load(g_app.playlist[idx])) {
        g_app.currentIndex = idx;
        snprintf(g_app.status, sizeof(g_app.status), "Lecture : %s", gfn(g_app.playlist[idx]).c_str());
    } else {
        snprintf(g_app.status, sizeof(g_app.status), "Impossible de lire ce fichier.");
    }
}
static void PlayNext() {
    if (g_app.playlist.empty()) return;
    int i = (g_app.currentIndex < 0) ? 0 : (g_app.currentIndex + 1) % (int)g_app.playlist.size();
    PlayIndex(i);
}
static void PlayPrev() {
    if (g_app.playlist.empty()) return;
    int i = (g_app.currentIndex < 0) ? 0 : (g_app.currentIndex - 1 + (int)g_app.playlist.size()) % (int)g_app.playlist.size();
    PlayIndex(i);
}
static void OpenFolderDialog(HWND hwnd) {
    WCHAR folder[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.hwndOwner = hwnd;
    bi.pszDisplayName = folder;
    bi.lpszTitle = L"Choisir un dossier de musique";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        WCHAR path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) ScanFolder(cvu(path));
        CoTaskMemFree(pidl);
    }
}
static void AddFilesDialog(HWND hwnd) {
    WCHAR buf[8192] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Fichiers audio\0*.mp3;*.wav;*.flac\0Tous les fichiers\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf) / sizeof(buf[0]);
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Ajouter des musiques";
    if (!GetOpenFileNameW(&ofn)) return;
    std::wstring dir(buf);
    const WCHAR* p = buf + dir.size() + 1;
    int added = 0;
    if (*p == L'\0') {
        AddPathIfAudio(cvu(dir));
        added = 1;
    } else {
        while (*p) {
            std::wstring name(p);
            AddPathIfAudio(cvu(dir + L"\\" + name));
            added++;
            p += name.size() + 1;
        }
    }
    snprintf(g_app.status, sizeof(g_app.status), "%d fichier(s) ajoute(s)", added);
    SaveConfig();
}
static void ChooseBackgroundDialog(HWND hwnd) {
    WCHAR buf[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.gif\0Tous les fichiers\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf) / sizeof(buf[0]);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Choisir un fond d'ecran";
    if (GetOpenFileNameW(&ofn)) {
        if (g_app.bg.LoadFromFile(cvu(buf))) SaveConfig();
    }
}
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}
void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = 0;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL flArr[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, flArr, 2,
                                                 D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, flArr, 2,
                                             D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext);
    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}
void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = LOWORD(lParam);
        g_ResizeHeight = HIWORD(lParam);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 640;
        mmi->ptMinTrackSize.y = 560;
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        UINT n = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
        int added = 0;
        for (UINT i = 0; i < n; i++) {
            WCHAR path[MAX_PATH];
            DragQueryFileW(hDrop, i, path, MAX_PATH);
            std::string pathUtf8 = cvu(path);
            if (chk(pathUtf8)) { g_app.playlist.push_back(pathUtf8); added++; }
        }
        DragFinish(hDrop);
        if (added) {
            snprintf(g_app.status, sizeof(g_app.status), "%d fichier(s) ajoute(s) par glisser-depose", added);
            SaveConfig();
        }
        return 0;
    }
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
static void SetupStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 10.0f;
    st.FrameRounding = 7.0f;
    st.GrabRounding = 8.0f;
    st.ChildRounding = 10.0f;
    st.PopupRounding = 8.0f;
    st.FramePadding = ImVec2(10, 7);
    st.ItemSpacing = ImVec2(10, 10);
    st.ScrollbarSize = 12.0f;
    st.WindowBorderSize = 0.0f;
    st.ChildBorderSize = 1.0f;
    ImVec4* c = st.Colors;
    ImVec4 panel = ImVec4(0.08f, 0.08f, 0.10f, 0.40f);
    ImVec4 accent = ImVec4(1.00f, 0.35f, 0.55f, 1.00f);
    ImVec4 accent2 = ImVec4(0.47f, 0.66f, 1.00f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.09f, 0.55f);
    c[ImGuiCol_ChildBg] = panel;
    c[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.18f, 0.55f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.26f, 0.65f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.30f, 0.75f);
    c[ImGuiCol_Button] = ImVec4(0.16f, 0.16f, 0.20f, 0.75f);
    c[ImGuiCol_ButtonHovered] = accent;
    c[ImGuiCol_ButtonActive] = accent2;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = accent2;
    c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.85f);
    c[ImGuiCol_HeaderHovered] = ImVec4(accent2.x, accent2.y, accent2.z, 0.55f);
    c[ImGuiCol_HeaderActive] = accent;
    c[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.97f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.75f, 0.73f, 0.78f, 0.85f);
    c[ImGuiCol_Border] = ImVec4(1,1,1,0.10f);
    c[ImGuiCol_Separator] = ImVec4(1,1,1,0.12f);
    c[ImGuiCol_TitleBgActive] = panel;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0,0,0,0.15f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1,1,1,0.20f);
    c[ImGuiCol_ScrollbarGrabHovered] = accent2;
    c[ImGuiCol_ScrollbarGrabActive] = accent;
}
static void DrawUI(HWND hwnd) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec4 pink = ImVec4(1.0f, 0.55f, 0.7f, 1.0f);
    if (g_app.bg.srv && g_app.bg.w > 0 && g_app.bg.h > 0) {
        float imgAspect = (float)g_app.bg.w / (float)g_app.bg.h;
        float dispAspect = io.DisplaySize.x / io.DisplaySize.y;
        ImVec2 uv0(0, 0), uv1(1, 1);
        if (imgAspect > dispAspect) {
            float visible = dispAspect / imgAspect;
            float excess = (1.0f - visible) * 0.5f;
            uv0.x = excess; uv1.x = 1.0f - excess;
        } else if (imgAspect < dispAspect) {
            float visible = imgAspect / dispAspect;
            float excess = (1.0f - visible) * 0.5f;
            uv0.y = excess; uv1.y = 1.0f - excess;
        }
        ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(intptr_t)g_app.bg.srv,
            ImVec2(0, 0), io.DisplaySize, uv0, uv1);
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 60));
    }
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 18));
    ImGui::Begin("MusicCore", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushFont(g_f1);
    ImGui::TextColored(pink, "# Music Core :p");
    ImGui::PopFont();
    ImGui::Spacing();
    if (ImGui::Button("Ajouter...", ImVec2(122, 0))) AddFilesDialog(hwnd);
    ImGui::SameLine();
    if (ImGui::Button("Fond d'ecran...", ImVec2(150, 0))) ChooseBackgroundDialog(hwnd);
    ImGui::Dummy(ImVec2(0, 10));
    float fullW = ImGui::GetContentRegionAvail().x;
    float reservedBottom = 420.0f;
    float topH = ImGui::GetContentRegionAvail().y - reservedBottom;
    if (topH < 150.0f) topH = 150.0f;
    float listW = fullW * 0.55f;
    float rightW = fullW - listW - 14.0f;
    ImGui::BeginChild("playlist", ImVec2(listW, topH), true);
    ImGui::TextDisabled("PLAYLIST - %d morceau(x)", (int)g_app.playlist.size());
    ImGui::Separator();
    for (int i = 0; i < (int)g_app.playlist.size(); i++) {
        bool isCur = (i == g_app.currentIndex);
        std::string display = sxt(gfn(g_app.playlist[i]));
        std::string label = (isCur ? std::string("> ") : std::string("   ")) + display;
        ImGui::PushID(i);
        if (isCur) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1.0f, 0.35f, 0.55f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        }
        ImGui::Selectable(label.c_str(), isCur);
        if (isCur) ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            PlayIndex(i);
        ImGui::PopID();
    }
    if (g_app.playlist.empty())
        ImGui::TextDisabled("Aucune musique.\nUtilise Ajouter,\nou glisse des fichiers ici.");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("apercu", ImVec2(rightW, topH), true);
    ImGui::TextDisabled("LECTURE EN COURS");
    ImGui::Separator();
    ImGui::Spacing();
    if (g_app.currentIndex >= 0) {
        ImGui::PushFont(g_f1);
        ImGui::PushStyleColor(ImGuiCol_Text, pink);
        ImGui::TextWrapped("%s", sxt(gfn(g_app.playlist[g_app.currentIndex])).c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::TextDisabled("Piste %d / %d", g_app.currentIndex + 1, (int)g_app.playlist.size());
    } else {
        ImGui::TextDisabled("-- Aucun morceau --");
    }
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0, 12));
    ma_uint64 cursorF = 0, lengthF = 0;
    float ratio = 0.0f; int curSec = 0, lenSec = 0;
    if (g_app.audio.soundLoaded) {
        ma_sound_get_cursor_in_pcm_frames(&g_app.audio.sound, &cursorF);
        ma_sound_get_length_in_pcm_frames(&g_app.audio.sound, &lengthF);
        ma_uint32 sr = ma_engine_get_sample_rate(&g_app.audio.engine);
        if (lengthF > 0) ratio = (float)((double)cursorF / (double)lengthF);
        curSec = (int)(cursorF / sr); lenSec = (int)(lengthF / sr);
        if (ma_sound_at_end(&g_app.audio.sound)) PlayNext();
    }
    if (!g_app.seeking) g_app.seekPreview = ratio;
    ImGui::SetNextItemWidth(fullW);
    if (ImGui::SliderFloat("##seek", &g_app.seekPreview, 0.0f, 1.0f, "")) {
        g_app.seeking = true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && g_app.audio.soundLoaded) {
        ma_sound_seek_to_pcm_frame(&g_app.audio.sound, (ma_uint64)(g_app.seekPreview * lengthF));
        g_app.seeking = false;
    }
    ImGui::Text("%02d:%02d / %02d:%02d", curSec/60, curSec%60, lenSec/60, lenSec%60);
    ImGui::Spacing();
    if (ImGui::Button("|<< Prec", ImVec2(90, 0))) PlayPrev();
    ImGui::SameLine();
    bool playing = g_app.audio.soundLoaded && ma_sound_is_playing(&g_app.audio.sound);
    if (ImGui::Button(playing ? "Pause" : "Lecture", ImVec2(90, 0))) {
        if (!g_app.audio.soundLoaded && !g_app.playlist.empty()) PlayIndex(g_app.currentIndex < 0 ? 0 : g_app.currentIndex);
        else g_app.audio.TogglePause();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(90, 0))) g_app.audio.Stop();
    ImGui::SameLine();
    if (ImGui::Button("Suiv >>|", ImVec2(90, 0))) PlayNext();
    ImGui::Dummy(ImVec2(0, 6));
    float halfW = (fullW - 24.0f) * 0.5f;
    ImGui::SetNextItemWidth(halfW);
    int volPct = (int)(g_app.audio.volume * 100);
    if (ImGui::SliderInt("Volume", &volPct, 0, 100, "%d%%")) {
        g_app.audio.volume = volPct / 100.0f;
        if (g_app.audio.soundLoaded) ma_sound_set_volume(&g_app.audio.sound, g_app.audio.volume);
    }
    ImGui::SameLine(0, 24);
    ImGui::SetNextItemWidth(halfW);
    if (ImGui::SliderInt("Flou du fond", &g_app.bg.blurRadius, 0, 20, "%d")) {
        g_app.bg.ApplyBlurToCurrentFrame();
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        g_app.bg.ApplyBlur();
        SaveConfig();
    }
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextColored(pink, "EGALISEUR");
    ImGui::Separator();
    for (int i = 0; i < NUM_BANDS; i++) {
        ImGui::BeginGroup();
        float g = (float)g_app.audio.eqGains[i];
        ImGui::PushID(i);
        if (ImGui::VSliderFloat("##eq", ImVec2(42, 110), &g, 12.0f, -12.0f, "%.0f")) {
            g_app.audio.SetGain(i, g);
        }
        ImGui::PopID();
        ImGui::TextUnformatted(BAND_LABELS[i]);
        ImGui::EndGroup();
        if (i < NUM_BANDS - 1) ImGui::SameLine();
    }
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Separator();
    ImGui::TextDisabled("%s", g_app.status);
    ImGui::End();
    ImGui::PopStyleVar();
}
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"MusicCoreClass", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"MusicCore", WS_OVERLAPPEDWINDOW,
                                 100, 100, 960, 820, nullptr, nullptr, wc.hInstance, nullptr);
    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); ::UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1; }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
    ::DragAcceptFiles(hwnd, TRUE);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    SetupStyle();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    char winDir[MAX_PATH] = {};
    GetWindowsDirectoryA(winDir, MAX_PATH);
    std::string fontRegularPath = std::string(winDir) + "\\Fonts\\segoeui.ttf";
    std::string fontBoldPath = std::string(winDir) + "\\Fonts\\segoeuib.ttf";
    ImFontConfig fontCfg;
    fontCfg.OversampleH = 2;
    fontCfg.OversampleV = 2;
    g_f2 = io.Fonts->AddFontFromFileTTF(fontRegularPath.c_str(), 18.0f, &fontCfg, io.Fonts->GetGlyphRangesDefault());
    if (!g_f2) g_f2 = io.Fonts->AddFontDefault();
    ImFontConfig titleCfg;
    titleCfg.OversampleH = 2;
    titleCfg.OversampleV = 2;
    g_f1 = io.Fonts->AddFontFromFileTTF(fontBoldPath.c_str(), 28.0f, &titleCfg, io.Fonts->GetGlyphRangesDefault());
    if (!g_f1) g_f1 = g_f2;
    io.FontDefault = g_f2;
    g_app.audio.Init();
    g_app.bg.device = g_pd3dDevice;
    g_app.bg.ctx = g_pd3dDeviceContext;
    LoadConfig();
    g_app.bg.EnsureTexture();
    g_app.bg.UploadCurrentFrame();
    DWORD lastTick = GetTickCount();
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10); continue;
        }
        g_SwapChainOccluded = false;
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }
        DWORD now = GetTickCount();
        g_app.bg.Tick((int)(now - lastTick));
        lastTick = now;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawUI(hwnd);
        ImGui::Render();
        const float clear[4] = { 0, 0, 0, 1 };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }
    SaveConfig();
    g_app.audio.Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
    return 0;
}
