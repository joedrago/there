#include "targetver.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include "resource.h"
#include "cJSON.h"

#include <stdio.h>
#include <sstream>
#include <string>
#include <vector>

#define WM_SHELLICONCLICKED (WM_USER + 1)

struct Action
{
    int id;
    unsigned int mods;
    unsigned int key;
    int x, y, w, h;
};
typedef std::vector<Action> ActionList;
static ActionList actions_;

static std::string windowString_;

struct cJSONCleanup
{
    cJSONCleanup(cJSON * p) : p_(p) {}
    ~cJSONCleanup() { if (p_) cJSON_Delete(p_); }
    cJSON * p_;
};

static bool fatalError(const char * format, ...)
{
    char errorText[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(errorText, sizeof(errorText), format, args);
    MessageBox(NULL, errorText, "There: Fatal Error", MB_OK);
    return false;
}

static bool addHotKey(HWND hWnd, int id, UINT mods, UINT vk)
{
    if (!RegisterHotKey(hWnd, id, mods | MOD_NOREPEAT, vk)) {
        return fatalError("Failed to register hotkey %d", id);
    }
    return true;
}

static bool registerHotKeys(HWND hWnd)
{
    for (ActionList::iterator it = actions_.begin(); it != actions_.end(); ++it) {
        if (!addHotKey(hWnd, it->id, it->mods, it->key)) {
            return fatalError("Failed to register hot key index %d", it->id - 1);
        }
    }
    return true;
}

static void moveForegroundWindow(Action & action, HWND unlessItIsThisWindow)
{
    HWND foregroundWindow = GetForegroundWindow();
    if (foregroundWindow == unlessItIsThisWindow) {
        return;
    }

    RECT destinationRect = { action.x, action.y, action.w, action.h };

    // Awful hack: Windows like Visual Studio draw their own borders and completely lie about
    // where their window rect is. Adjust the move location accordingly.
    {
        RECT windowRect;
        GetWindowRect(foregroundWindow, &windowRect);
        RECT clientRect;
        GetClientRect(foregroundWindow, &clientRect);
        if (((windowRect.right - windowRect.left) == clientRect.right) && ((windowRect.bottom - windowRect.top) == clientRect.bottom)) {
            destinationRect.left += 7;   // TODO: Replace magic number
            destinationRect.right -= 14; // TODO: Replace magic number
            destinationRect.bottom -= 7; // TODO: Replace magic number
        }
    }

    MoveWindow(foregroundWindow, destinationRect.left, destinationRect.top, destinationRect.right, destinationRect.bottom, TRUE);
}

static void onHotKey(HWND hDlg, int id)
{
    for (ActionList::iterator it = actions_.begin(); it != actions_.end(); ++it) {
        if (it->id == id) {
            moveForegroundWindow(*it, hDlg);
        }
    }
}

static void tick(HWND hDlg)
{
    HWND foregroundWindow = GetForegroundWindow();
    if (foregroundWindow == hDlg) {
        return;
    }

    RECT r;
    GetWindowRect(foregroundWindow, &r);

    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "{ \"key\": \"alt win up\", \"rect\": [%d, %d, %d, %d] },", r.left, r.top, r.right - r.left, r.bottom - r.top);
    std::string newWindowString = buffer;

    if (newWindowString != windowString_) {
        windowString_ = newWindowString;
        SetWindowText(GetDlgItem(hDlg, IDC_TEXT), windowString_.c_str());
    }
}

// static void paint(HWND hDlg)
// {
//     PAINTSTRUCT ps;
//     HDC hdc = BeginPaint(hDlg, &ps);
//     RECT r;
//     GetClientRect(hDlg, &r);
//     SetBkMode(hdc, TRANSPARENT);
//     DrawText(hdc, windowString_.c_str(), windowString_.size(), &r, DT_LEFT);
//     EndPaint(hDlg, &ps);
// }

static void createShellIcon(HWND hDlg)
{
    NOTIFYICONDATA nid;

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hDlg;
    nid.uID = 100;
    nid.uVersion = NOTIFYICON_VERSION;
    nid.uCallbackMessage = WM_SHELLICONCLICKED;
    nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_THERE));
    strcpy(nid.szTip, "There");
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;

    Shell_NotifyIcon(NIM_ADD, &nid);
}

static void destroyShellIcon(HWND hDlg)
{
    NOTIFYICONDATA nid;
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hDlg;
    nid.uID = 100;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

static INT_PTR CALLBACK Proc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_INITDIALOG:
            registerHotKeys(hDlg);
            SetTimer(hDlg, 5, 1000, NULL);
            createShellIcon(hDlg);
            return (INT_PTR)TRUE;

        case WM_SHELLICONCLICKED:
            switch (lParam) {
                case WM_LBUTTONDBLCLK:
                    ShowWindow(hDlg, SW_SHOW);
                    break;
            }
            return (INT_PTR)TRUE;

        case WM_TIMER:
            if (IsWindowVisible(hDlg)) {
                tick(hDlg);
            }
            return (INT_PTR)TRUE;

        // case WM_PAINT:
        //     paint(hDlg);
        //     return (INT_PTR)TRUE;

        case WM_HOTKEY:
            onHotKey(hDlg, (int)wParam);
            return (INT_PTR)TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_QUIT) {
                destroyShellIcon(hDlg);
                PostQuitMessage(0);
                return (INT_PTR)TRUE;
            } else if (LOWORD(wParam) == IDCANCEL) {
                ShowWindow(hDlg, SW_HIDE);
                return (INT_PTR)TRUE;
            }
            break;
    }
    return (INT_PTR)FALSE;
}

// taken shamelessly from http://stackoverflow.com/questions/236129/split-a-string-in-c
static void split(const std::string & s, char delim, std::vector<std::string> & elems)
{
    std::stringstream ss;
    ss.str(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
}

static bool parseHotKey(int index, const std::string & key, Action & action)
{
    std::vector<std::string> pieces;
    split(key, ' ', pieces);
    if (pieces.empty()) {
        return fatalError("action %d has an empty key string", index);
    }

    action.mods = 0;
    action.key = 0;
    for (std::vector<std::string>::iterator it = pieces.begin(); it != pieces.end(); ++it) {
        if (*it == "win") {
            action.mods |= MOD_WIN;
        } else if (*it == "alt") {
            action.mods |= MOD_ALT;
        } else if ((*it == "ctrl") || (*it == "ctl") || (*it == "control")) {
            action.mods |= MOD_CONTROL;
        } else if (*it == "shift") {
            action.mods |= MOD_SHIFT;
        } else if (*it == "up") {
            action.key = VK_UP;
        } else if (*it == "down") {
            action.key = VK_DOWN;
        } else if (*it == "left") {
            action.key = VK_LEFT;
        } else if (*it == "space") {
            action.key = VK_SPACE;
        } else if (*it == "right") {
            action.key = VK_RIGHT;
        } else {
            if (it->size() != 1) {
                return fatalError("action %d has an unknown key element: %s", index, it->c_str());
            }
            action.key = (unsigned int)((*it)[0]);
        }
    }
    if (action.key == 0) {
        return fatalError("action %d did not provide an actual key to press", index);
    }
    return true;
}

static bool loadHotKeys()
{
    actions_.clear();

    char buffer[MAX_PATH];
    if (GetModuleFileName(NULL, (char *)buffer, MAX_PATH) == ERROR_INSUFFICIENT_BUFFER) {
        return fatalError("Not enough room to get module filename?");
    }

    std::string modulePath = buffer;
    size_t pos = modulePath.find_last_of('\\');
    if (pos == std::string::npos) {
        return fatalError("Module filename does not contain a backslash?");
    }

    std::string jsonPath = modulePath.substr(0, pos) + "\\there.json";
    FILE * f = fopen(jsonPath.c_str(), "rb");
    if (!f) {
        return fatalError("there.json does not exist next to there.exe");
    }

    fseek(f, 0, SEEK_END);
    int len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 1) {
        return fatalError("there.json is empty");
    }

    std::vector<char> json(len + 1);
    int bytesRead = fread(&json[0], 1, len, f);
    fclose(f);
    if (bytesRead != len) {
        return fatalError("failed to read all %d bytes of there.json", len);
    }

    cJSON * root = cJSON_Parse(&json[0]);
    cJSONCleanup cleanup(root);
    if (!root) {
        return fatalError("there.json is invalid JSON");
    }
    if (root->type != cJSON_Object) {
        return fatalError("there.json is not an object at the root");
    }
    cJSON * actions = cJSON_GetObjectItem(root, "actions");
    if (!actions || (actions->type != cJSON_Array)) {
        return fatalError("JSON does not contain an 'actions' array");
    }
    int index = 0;
    for (cJSON * jAction = actions->child; jAction; ++index, jAction = jAction->next) {
        Action action;
        action.id = index + 1;

        if (jAction->type != cJSON_Object) {
            return fatalError("action %d is not an object", index);
        }

        cJSON * jKey = cJSON_GetObjectItem(jAction, "key");
        if (!jKey || (jKey->type != cJSON_String)) {
            return fatalError("action %d does not have a 'key' string value", index);
        }
        std::string key = jKey->valuestring;
        if (!parseHotKey(index, key, action)) {
            return false;
        }

        cJSON * jRect = cJSON_GetObjectItem(jAction, "rect");
        if (!jRect || (jRect->type != cJSON_Array)) {
            return fatalError("action %d does not have a 'rect' array value", index);
        }
        if (cJSON_GetArraySize(jRect) != 4) {
            return fatalError("action %d 'rect' array doesn't have 4 values", index);
        }
        int nums[4];
        for (int i = 0; i < 4; ++i) {
            cJSON * jNum = cJSON_GetArrayItem(jRect, i);
            if (!jNum || (jNum->type != cJSON_Number)) {
                return fatalError("action %d has a non-number for rect index %d", index, i);
            }
            nums[i] = jNum->valueint;
        }
        action.x = nums[0];
        action.y = nums[1];
        action.w = nums[2];
        action.h = nums[3];
        actions_.push_back(action);
    }
    return true;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    if (!loadHotKeys()) {

        return 0;
    }

    HWND hDlg = CreateDialog(hInstance, MAKEINTRESOURCE(IDD_THERE), NULL, Proc);
    ShowWindow(hDlg, SW_HIDE);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        // if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    return 0;
}
