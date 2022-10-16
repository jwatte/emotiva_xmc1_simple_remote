#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <Windows.h>
#include <Windowsx.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <vector>
#include <string>

#pragma warning(disable : 4996)

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

void error(char const *str)
{
    ::MessageBoxA(NULL, str, "Error", MB_OK);
    ::ExitProcess(1);
}

bool running = true;
bool connected = false;
struct sockaddr_in avraddr = {};
SOCKET avrsock = INVALID_SOCKET;
std::vector<std::string> inputs;
int selectedinput;
bool powerstate;
float volumestate;
std::string message;
HWND hGui;
int desireVolume = -40;
const int maxVolume = 0;
const int minVolume = -60;
int volume = -40;

double lastreceived;
double lastupdatetime;

double performanceFrequency()
{
    uint64_t xx;
    ::QueryPerformanceFrequency((LARGE_INTEGER *)&xx);
    return (double)xx;
}

double tickDuration = 1.0 / performanceFrequency();

double clocktime()
{
    uint64_t xx = 0;
    ::QueryPerformanceCounter((LARGE_INTEGER *)&xx);
    return xx * tickDuration;
}

bool maybeGetAttr(char const *buf, char const *tag, char const *attr, std::string &value)
{
    char tag2[64];
    snprintf(tag2, 64, "<%s %s=\"", tag, attr);
    tag2[63] = 0;
    char const *p = strstr(buf, tag2);
    if (p != nullptr)
    {
        p += strlen(tag2);
        char const *end = strstr(p, "\"");
        if (end != nullptr)
        {
            value = std::string(p, end);
            return true;
        }
    }
    return false;
}

CRITICAL_SECTION locksubs;

struct subthing
{
    std::string name;
    std::string lastValue;
    int xpos;
    int ypos;
    void (*render)(HDC hdc, RECT *area, subthing *sub);
    RECT area;
    bool down;
    bool selected;
};

void drawPower(HDC, RECT *, subthing *);
void drawInput(HDC, RECT *, subthing *);
void drawVolume(HDC, RECT *, subthing *);
void drawNothing(HDC, RECT *, subthing *);

subthing subs[] = {
    {"power", "", 0, 0, drawPower},
    {"volume", "", 0, 0, drawVolume},
    {"source", "", 0, 0, drawNothing},
    {"input_1", "", 0, 0, drawInput},
    {"input_2", "", 1, 0, drawInput},
    {"input_3", "", 2, 0, drawInput},
    {"input_4", "", 3, 0, drawInput},
    {"input_5", "", 0, 1, drawInput},
    {"input_6", "", 1, 1, drawInput},
    {"input_7", "", 2, 1, drawInput},
    {"input_8", "", 3, 1, drawInput},
};

void drawInput(HDC hdc, RECT *crect, subthing *sub)
{
    RECT area = *crect;
    int iw = (area.right - area.left - 50) / 4;
    int ih = (area.bottom - area.top - 50) / 4;
    int il = crect->left + 10 + (iw + 10) * sub->xpos;
    int it = crect->top + 10 + (ih + 10) * (sub->ypos + 1);
    area.left = il;
    area.top = it;
    area.right = il + iw;
    area.bottom = it + ih;
    sub->area = area;
    HBRUSH bgobj;
    COLORREF textcol;
    if (sub->down)
    {
        bgobj = (HBRUSH)::GetStockObject(WHITE_BRUSH);
        textcol = RGB(0, 0, 0);
    }
    else if (sub->lastValue == subs[2].lastValue)
    {
        bgobj = (HBRUSH)::GetStockObject(LTGRAY_BRUSH);
        textcol = RGB(0, 0, 0);
    }
    else
    {
        bgobj = (HBRUSH)::GetStockObject(DKGRAY_BRUSH);
        textcol = RGB(192, 192, 192);
    }
    ::SelectObject(hdc, ::GetStockObject(DC_PEN));
    ::SetDCPenColor(hdc, textcol);
    ::FillRect(hdc, &area, bgobj);
    ::SetTextColor(hdc, textcol);
    ::SetBkMode(hdc, TRANSPARENT);
    ::DrawTextA(hdc, sub->lastValue.c_str(), -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void drawPower(HDC hdc, RECT *crect, subthing *sub)
{
    RECT area = *crect;
    int ih = (area.bottom - area.top - 50) / 4;
    area.left = crect->left + 10;
    area.bottom = crect->bottom - 10;
    area.top = area.bottom - ih;
    area.right = area.left + ih;
    sub->area = area;
    ::SelectObject(hdc, ::GetStockObject(DC_PEN));
    ::SetDCPenColor(hdc, RGB(128, 128, 128));
    HBRUSH bgobj;
    COLORREF textcol;
    if (sub->down)
    {
        bgobj = (HBRUSH)::GetStockObject(WHITE_BRUSH);
        textcol = RGB(0, 0, 0);
    }
    else if (sub->lastValue == "On")
    {
        bgobj = (HBRUSH)::GetStockObject(LTGRAY_BRUSH);
        textcol = RGB(0, 0, 0);
    }
    else
    {
        bgobj = (HBRUSH)::GetStockObject(DKGRAY_BRUSH);
        textcol = RGB(192, 192, 192);
    }
    ::FillRect(hdc, &area, bgobj);
    ::SetTextColor(hdc, textcol);
    ::SetBkMode(hdc, TRANSPARENT);
    ::DrawTextA(hdc, sub->lastValue.c_str(), -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void drawVolume(HDC hdc, RECT *crect, subthing *sub)
{
    RECT area = *crect;
    int ih = (area.bottom - area.top - 50) / 4;
    area.left = crect->left + 20 + ih;
    area.bottom = crect->bottom - 10;
    area.top = area.bottom - ih;
    area.right = crect->right - 10;
    sub->area = area;
    ::SelectObject(hdc, ::GetStockObject(DC_PEN));
    ::SetDCPenColor(hdc, RGB(128, 128, 128));
    ::FillRect(hdc, &area, (HBRUSH)::GetStockObject(DKGRAY_BRUSH));
    ::SetTextColor(hdc, RGB(192, 96, 96));
    RECT a2 = area;
    a2.right = a2.left + (a2.bottom - a2.top);
    int delta = (area.right - a2.right) * (volume - minVolume) / (maxVolume - minVolume);
    a2.left += delta;
    a2.right += delta;
    ::FillRect(hdc, &a2, (HBRUSH)::GetStockObject(LTGRAY_BRUSH));
    ::SetBkMode(hdc, TRANSPARENT);
    ::DrawTextA(hdc, sub->lastValue.c_str(), -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void drawNothing(HDC hdc, RECT *area, subthing *sub)
{
}

void sendupdate(char const *key, char const *value)
{
    std::string cmd;
    cmd += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
           "<emotivaControl>\n";
    cmd += std::string("<") + key + " value=\"" + value + "\" />\n";
    cmd += "</emotivaControl>\n";
    if (avrsock != INVALID_SOCKET)
    {
        if (sendto(avrsock, cmd.c_str(), cmd.length(), 0, (sockaddr *)&avraddr, sizeof(avraddr)) == SOCKET_ERROR)
        {
            message = "Failed to send command";
        }
    }
}

void sendPower(bool power)
{
    sendupdate(power ? "power_on" : "power_off", "0");
}

void sendVolume(int vol)
{
    char str[50];
    sprintf(str, "%d.0", desireVolume);
    sendupdate("set_volume", str);
}

void sendInput(std::string const &input)
{
    char const *us = strchr(input.c_str(), '_');
    if (us == nullptr)
    {
        return;
    }
    char buf[50];
    sprintf(buf, "source%s", us);
    sendupdate(buf, "0");
}

bool ParsePoll(char const *buf, size_t len)
{
    ::EnterCriticalSection(&locksubs);
    if (strstr(buf, "<emotivaTransponder"))
    {
        if (!connected)
        {
            lastreceived = clocktime();
            lastupdatetime = 0.0;
        }
        ::LeaveCriticalSection(&locksubs);
        return true;
    }
    else if (strstr(buf, "<emotivaUpdate") || strstr(buf, "<emotivaNotify") || strstr(buf, "<emotivaSubscription"))
    {
        lastreceived = clocktime();
        for (int i = 0; i != sizeof(subs) / sizeof(subs[0]); ++i)
        {
            std::string v;
            if (maybeGetAttr(buf, subs[i].name.c_str(), "value", v))
            {
                subs[i].lastValue = v;
            }
        }
        char *s;
        double v = strtod(subs[1].lastValue.c_str(), &s);
        volume = (int)(v + 0.05);
        ::LeaveCriticalSection(&locksubs);
        return true;
    }
    ::LeaveCriticalSection(&locksubs);
    return false;
}

DWORD WINAPI Updater(LPVOID)
{
    while (connected)
    {
        double now = clocktime();
        if (now - lastreceived > 3.0)
        {
            connected = false;
            break;
        }
        SOCKET s = avrsock;
        if (now - lastupdatetime > 0.5)
        {
            lastupdatetime = now;
            ::EnterCriticalSection(&locksubs);
            if (s != INVALID_SOCKET)
            {
                std::string sub = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<emotivaUpdate>\n";
                for (auto const &it : subs)
                {

                    sub = sub + "<" + it.name + " />\n";
                }
                sub = sub + "</emotivaUpdate>\n";
                if (sendto(s, sub.c_str(), sub.size(), 0, (sockaddr const *)&avraddr, (int)sizeof(avraddr)) == SOCKET_ERROR)
                {
                    connected = false;
                    char estr[1000];
                    sprintf(estr, "sendto failed: %d", WSAGetLastError());
                    message = estr;
                    ::InvalidateRect(hGui, NULL, TRUE);
                    ::LeaveCriticalSection(&locksubs);
                    break;
                }
            }
            ::LeaveCriticalSection(&locksubs);
        }
        if (s != INVALID_SOCKET)
        {
            fd_set set = {0};
            FD_SET(s, &set);
            struct timeval wait = {0, 100000};
            select(s + 1, &set, nullptr, nullptr, &wait);
            if (FD_ISSET(s, &set))
            {
                char buf[10000];
                int len = recv(s, buf, 9999, 0);
                if (len > 0)
                {
                    buf[len] = 0;
                    if (ParsePoll(buf, len))
                    {
                        ::InvalidateRect(hGui, nullptr, FALSE);
                    }
                }
            }
        }
    }
    return 0;
}

DWORD WINAPI Poller(LPVOID)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
    {
        char estr[1000];
        sprintf(estr, "Could not open socket: %d", ::WSAGetLastError());
        error(estr);
    }
    struct sockaddr_in sin = {0};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(7001);
    if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) == SOCKET_ERROR)
    {
        char estr[1000];
        sprintf(estr, "Could not bind socket: %d", ::WSAGetLastError());
        error(estr);
    }
    int one = 1;
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char const *)&one, sizeof(one)) == SOCKET_ERROR)
    {
        char estr[1000];
        sprintf(estr, "Could not set options on socket: %d", ::WSAGetLastError());
        error(estr);
    }

    while (running)
    {
        if (!connected)
        {
            if (avrsock != INVALID_SOCKET)
            {
                closesocket(avrsock);
                avrsock = INVALID_SOCKET;
                message = "Lost connection";
            }
            char const *discover = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<emotivaPing />\n";
            size_t len = strlen(discover);
            struct sockaddr_in sin = {};
            sin.sin_family = AF_INET;
            sin.sin_port = htons(7000);
            sin.sin_addr.s_addr = INADDR_BROADCAST;
            if (sendto(s, discover, len, 0, (struct sockaddr *)&sin, sizeof(sin)) == SOCKET_ERROR)
            {
                char estr[1000];
                sprintf(estr, "Could not send broadcast: %d", ::WSAGetLastError());
                error(estr);
            }
        }
        struct timeval timo = {1, 0};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(s, &fds);
        if (select(s + 1, &fds, NULL, NULL, &timo) == SOCKET_ERROR)
        {
            char estr[1000];
            sprintf(estr, "Could not select: %d", ::WSAGetLastError());
            error(estr);
        }
        if (FD_ISSET(s, &fds))
        {
            char buf[5000];
            struct sockaddr_in sin = {};
            int sinlen = sizeof(sin);
            int len = recvfrom(s, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&sin, &sinlen);
            if (len == SOCKET_ERROR)
            {
                char estr[1000];
                sprintf(estr, "Could not receive: %d", ::WSAGetLastError());
                error(estr);
            }
            buf[len] = 0;
            if (ParsePoll(buf, len))
            {
                lastreceived = clocktime();
                connected = true;
                avraddr = sin;
                avraddr.sin_port = htons(7002);
                sprintf(buf, "Connected to %d.%d.%d.%d", sin.sin_addr.S_un.S_un_b.s_b1, sin.sin_addr.S_un.S_un_b.s_b2, sin.sin_addr.S_un.S_un_b.s_b3, sin.sin_addr.S_un.S_un_b.s_b4);
                message = buf;
                ::InvalidateRect(hGui, NULL, TRUE);
                avrsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                if (avrsock == INVALID_SOCKET)
                {
                    char estr[1000];
                    sprintf(estr, "Could not open socket: %d", ::WSAGetLastError());
                    message = estr;
                    continue;
                }
                struct sockaddr_in baddr = {};
                baddr.sin_family = AF_INET;
                baddr.sin_port = htons(7002);
                if (bind(avrsock, (struct sockaddr *)&baddr, sizeof(baddr)) == SOCKET_ERROR)
                {
                    char estr[1000];
                    sprintf(estr, "Could not bind socket: %d", ::WSAGetLastError());
                    message = estr;
                    continue;
                }
                ::CreateThread(NULL, 0, Updater, NULL, 0, NULL);
            }
        }
    }

    closesocket(s);
    return 0;
}

void GetMessageArea(RECT *crect, RECT *area)
{
    area->left = crect->left + 10;
    area->top = crect->top + 10;
    area->right = crect->right - 10;
    area->bottom = ((crect->bottom - crect->top) - 20) / 4 + 5;
}

void DrawWindow(HWND hwnd, HDC hdc)
{
    ::EnterCriticalSection(&locksubs);
    RECT rect = {};
    ::GetClientRect(hwnd, &rect);
    ::FillRect(hdc, &rect, (HBRUSH)::GetStockObject(BLACK_BRUSH));

    RECT area = {};
    GetMessageArea(&rect, &area);
    if (!connected)
    {
        ::SetTextColor(hdc, RGB(255, 255, 255));
        ::SetBkMode(hdc, TRANSPARENT);
        ::DrawTextA(hdc, "Searching for Emotiva...", -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else
    {
        ::SetTextColor(hdc, RGB(255, 255, 255));
        ::SetBkMode(hdc, TRANSPARENT);
        char const *msg = message.c_str();
        ::DrawTextA(hdc, msg, -1, &area, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    for (auto &sub : subs)
    {
        sub.render(hdc, &rect, &sub);
    }

    ::LeaveCriticalSection(&locksubs);
}

bool dispatchClick(HWND hwnd, subthing *thing, POINT pt)
{
    if (!PtInRect(&thing->area, pt))
    {
        return false;
    }
    if (thing->render == drawInput)
    {
        return true;
    }
    else if (thing->render == drawPower)
    {
        return true;
    }
    else if (thing->render == drawVolume)
    {
        int vol = minVolume + (maxVolume - minVolume) * (pt.x - thing->area.left) / (thing->area.right - thing->area.left);
        if (vol != desireVolume)
        {
            desireVolume = vol;
            sendVolume(desireVolume);
        }
        return true;
    }
    else
    {
        return false;
    }
}

void handleClick(subthing *thing)
{
    if (thing->render == drawPower)
    {
        sendPower(thing->lastValue != "On");
    }
    else if (thing->render == drawInput)
    {
        sendInput(thing->name);
    }
}

subthing *tracking;

LRESULT CALLBACK MyWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_CREATE:
        ::CreateThread(NULL, 0, Poller, NULL, 0, NULL);
        break;
    case WM_DESTROY:
        running = false;
        ::PostQuitMessage(0);
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps = {0};
        HDC hdc = ::BeginPaint(hwnd, &ps);
        DrawWindow(hwnd, hdc);
        ::EndPaint(hwnd, &ps);
    }
        return 0;
    case WM_LBUTTONDOWN:
    {
        ::EnterCriticalSection(&locksubs);
        if (tracking)
        {
            tracking->down = false;
            tracking = nullptr;
        }
        for (auto &st : subs)
        {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (PtInRect(&st.area, pt))
            {
                tracking = &st;
                st.down = true;
                if (!dispatchClick(hwnd, tracking, pt))
                {
                    tracking->down = false;
                    tracking = nullptr;
                }
                ::InvalidateRect(hwnd, NULL, TRUE);
                break;
            }
        }
        ::LeaveCriticalSection(&locksubs);
    }
    break;
    case WM_MOUSEMOVE:
    {
        ::EnterCriticalSection(&locksubs);
        if (tracking)
        {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (!dispatchClick(hwnd, tracking, pt))
            {
                tracking->down = false;
                tracking = nullptr;
            }
            ::InvalidateRect(hwnd, NULL, TRUE);
        }
        ::LeaveCriticalSection(&locksubs);
    }
    case WM_LBUTTONUP:
    {
        ::EnterCriticalSection(&locksubs);
        if (tracking)
        {
            subthing *hit = tracking;
            tracking->down = false;
            tracking = nullptr;
            handleClick(hit);
            ::InvalidateRect(hwnd, NULL, TRUE);
        }
        ::LeaveCriticalSection(&locksubs);
    }
    break;
    }
    return ::DefWindowProc(hwnd, msg, wparam, lparam);
}

void SetupClass()
{
    WNDCLASSEXW wcex = {0};
    wcex.cbSize = sizeof(wcex);
    wcex.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wcex.lpfnWndProc = MyWindowProc;
    wcex.hInstance = GetModuleHandle(NULL);
    wcex.lpszClassName = L"XmcRemoteClass";
    RegisterClassExW(&wcex);
}

void MakeWindow()
{
    hGui = ::CreateWindowExW(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE, L"XmcRemoteClass", L"XmcRemote", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 320, NULL, NULL, GetModuleHandle(NULL), NULL);
    ::ShowWindow(hGui, SW_SHOW);
}

INT __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, INT nCmdShow)
{
    InitializeCriticalSection(&locksubs);
    WSADATA wsaData = {0};
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SetupClass();
    MakeWindow();

    MSG msg;
    while (::GetMessage(&msg, NULL, 0, 0))
    {
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }

    WSACleanup();
    return 0;
}
