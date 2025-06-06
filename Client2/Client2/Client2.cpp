#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <string>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

SOCKET sock;
bool running = true;
HWND hTextBox, hInputBox, hSendButton;

// 修剪wstring，去除首尾的空格
wstring trim(const wstring& str) {
    size_t first = str.find_first_not_of(L" \t\r\n");
    if (first == wstring::npos) return L""; // No content
    size_t last = str.find_last_not_of(L" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void appendToTextBox(HWND hwnd, const wstring& newText) {
    // 获取当前文本框的内容长度
    int textLength = GetWindowTextLengthW(hwnd);

    // 获取当前文本框的内容
    wchar_t* currentText = new wchar_t[textLength + 1];
    GetWindowTextW(hwnd, currentText, textLength + 1);

    // 修整消息内容，去除多余空格
    wstring trimmedMessage = trim(newText);

    // 拼接新的消息（保持现有文本和新文本一起）
    wstring combinedText = wstring(currentText) + L"\r\n" + trimmedMessage;

    // 更新文本框内容
    SetWindowTextW(hwnd, combinedText.c_str());

    // 获取文本框的当前滚动位置
    DWORD dwPos = GetScrollPos(hwnd, SB_VERT);

    // 获取文本框的总行数
    int lineCount = SendMessage(hwnd, EM_GETLINECOUNT, 0, 0);

    // 获取文本框是否已滚动到底部（当最后一行与滚动条位置接近时）
    BOOL atBottom = (lineCount - dwPos <= 1);

    // 如果滚动条在最底部，自动滚动
    if (atBottom) {
        SendMessage(hwnd, EM_LINESCROLL, 0, INT_MAX);  // 滚动到底部
    }

    // 清理内存
    delete[] currentText;
}


void recvThreadFunc() {
    char buffer[1024];
    while (running) {
        int ret = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (ret <= 0) {
            cout << "Disconnected from server." << endl;
            running = false;
            break;
        }
        buffer[ret] = 0;
        string msg(buffer);

        // 按行分割显示（服务器发消息都是一行一条）
        size_t pos = 0;
        while ((pos = msg.find('\n')) != string::npos) {
            string line = msg.substr(0, pos);
            msg.erase(0, pos + 1);

            // 去除首尾空格
            line.erase(line.find_last_not_of(" \t\r\n") + 1);  // 去掉末尾空白
            line.erase(0, line.find_first_not_of(" \t\r\n"));  // 去掉开头空白

            // 判断是否广播消息
            if (line.rfind("[BROADCAST]", 0) == 0) {
                string formattedMessage = "\n【服务器广播】" + line.substr(11) + "\n";
                // 转换为宽字符并设置文本框内容
                wchar_t wformattedMessage[1024];
                MultiByteToWideChar(CP_ACP, 0, formattedMessage.c_str(), -1, wformattedMessage, 1024);
                appendToTextBox(hTextBox, wformattedMessage);
            }
            else if (line.rfind("[USERLIST]", 0) == 0) {
                string formattedMessage = "\n【在线用户】 " + line.substr(10) + "\n";
                wchar_t wformattedMessage[1024];
                MultiByteToWideChar(CP_ACP, 0, formattedMessage.c_str(), -1, wformattedMessage, 1024);
                appendToTextBox(hTextBox, wformattedMessage);
            }
            else {
                string formattedMessage = "\n" + line + "\n";
                wchar_t wformattedMessage[1024];
                MultiByteToWideChar(CP_ACP, 0, formattedMessage.c_str(), -1, wformattedMessage, 1024);
                appendToTextBox(hTextBox, wformattedMessage);
            }
        }
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {  // Send button clicked
            char msg[1024];
            GetWindowTextA(hInputBox, msg, sizeof(msg));

            strcat_s(msg, sizeof(msg), "\n");
            if (strlen(msg) > 0) {
                // 异步发送消息到服务器
                thread sendThread([msg]() {
                    send(sock, msg, (int)strlen(msg), 0);
                    });
                sendThread.detach();  // 分离线程，允许继续执行

                // 清空输入框
                SetWindowTextA(hInputBox, "");
            }
        }
        break;
    case WM_CLOSE:
        running = false;
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // 初始化Winsock

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8888);

    // 使用inet_pton替代inet_addr
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        MessageBox(NULL, L"Connect failed.", L"ERROR", MB_OK);
        WSACleanup();
        return 1;
    }
    // Win32窗口初始化
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ChatClientWindowClass";
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, L"ChatClientWindowClass", L"聊天客户端", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, nullptr, nullptr, hInstance, nullptr);

    // 创建文本框、输入框和按钮
    hTextBox = CreateWindowEx(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL| WS_BORDER,
        10, 10, 360, 150, hwnd, nullptr, hInstance, nullptr);

    hInputBox = CreateWindowEx(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
        10, 170, 280, 30, hwnd, nullptr, hInstance, nullptr);

    hSendButton = CreateWindow(L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        300, 170, 70, 30, hwnd, (HMENU)1, hInstance, nullptr);

    // 显示窗口
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    // 启动接收消息线程
    thread recvThread(recvThreadFunc);

    // 消息循环
    MSG msg;
    while (running) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // 关闭连接
    closesocket(sock);
    recvThread.join();
    WSACleanup();

    return 0;
}
