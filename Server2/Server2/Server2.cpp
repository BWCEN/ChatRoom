#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <map>
#include <string>
#include <mutex>
#include <algorithm>
#include <sstream>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

struct ClientInfo {
    SOCKET socket;
    string username;
};

struct Group {
    string groupName;
    vector<string> members; // 成员用户名列表
};

map<string, string> userDatabase;           // 用户名 -> 密码
vector<ClientInfo> connectedClients;        // 在线用户
map<string, Group> groups;                   // 群组信息
mutex clientsMutex;                          // 保护 connectedClients
mutex groupsMutex;                           // 保护 groups
bool serverRunning = true;

// Win32 API 窗口控件的全局变量
HWND hTextBox, hInputBox, hSendButton, hWnd;

void initializeUsers() {
    userDatabase["user1"] = "pass1";
    userDatabase["user2"] = "pass2";
    userDatabase["user3"] = "pass3";
}

// 读取一行直到 '\n'
string recvLine(SOCKET sock) {
    string result;
    char ch;
    int ret;
    while (true) {
        ret = recv(sock, &ch, 1, 0);
        if (ret <= 0) {
            return "<DISCONNECTED>";
        }
        if (ch == '\n') break;
        if (ch != '\r') result += ch;
    }
    result.erase(result.find_last_not_of(" \t\r\n") + 1);
    result.erase(0, result.find_first_not_of(" \t\r\n"));
    return result;
}

void sendLine(SOCKET sock, const string& msg) {
    string sendMsg = msg + "\n";
    send(sock, sendMsg.c_str(), (int)sendMsg.length(), 0);
}

void broadcastOnlineUsers() {
    lock_guard<mutex> lock(clientsMutex);
    string userList = "[USERLIST]";
    for (size_t i = 0; i < connectedClients.size(); ++i) {
        userList += connectedClients[i].username;
        if (i != connectedClients.size() - 1) userList += ",";
    }
    for (auto& client : connectedClients) {
        sendLine(client.socket, userList);
    }
}

// 发送消息到文本框
void appendToTextBox(HWND hwnd, const wstring& newText) {
    // 获取当前文本框的内容长度
    int textLength = GetWindowTextLengthW(hwnd);

    // 获取当前文本框的内容
    wchar_t* currentText = new wchar_t[textLength + 1];
    GetWindowTextW(hwnd, currentText, textLength + 1);

    // 拼接新的消息（保持现有文本和新文本一起）
    wstring combinedText = wstring(currentText) + L"\r\n" + newText;

    // 更新文本框内容
    SetWindowTextW(hwnd, combinedText.c_str());

    // 清理内存
    delete[] currentText;
}

// 打印在线用户
void printOnlineUsers() {
    lock_guard<mutex> lock(clientsMutex);
    wstring onlineUsers = L"当前在线用户：";  

    for (size_t i = 0; i < connectedClients.size(); ++i) {
        onlineUsers.append(connectedClients[i].username.begin(), connectedClients[i].username.end());  
        if (i != connectedClients.size() - 1)
            onlineUsers.append(L", ");  // 拼接逗号和空格
    }

    appendToTextBox(hTextBox, onlineUsers.c_str());  // 使用宽字符类型
}


// 发送群成员列表给指定客户端
void sendGroupMembers(SOCKET clientSock, const string& groupName) {
    lock_guard<mutex> lock(groupsMutex);
    auto it = groups.find(groupName);
    if (it == groups.end()) {
        sendLine(clientSock, "[ERROR] Group '" + groupName + "' does not exist.");
        return;
    }
    string membersStr = "[GROUP_MEMBERS] " + groupName + ": ";
    for (size_t i = 0; i < it->second.members.size(); ++i) {
        membersStr += it->second.members[i];
        if (i != it->second.members.size() - 1) membersStr += ",";
    }
    sendLine(clientSock, membersStr);
}

bool isUserOnline(const string& username) {
    lock_guard<mutex> lock(clientsMutex);
    for (auto& c : connectedClients) {
        if (c.username == username) return true;
    }
    return false;
}

SOCKET getUserSocket(const string& username) {
    lock_guard<mutex> lock(clientsMutex);
    for (auto& c : connectedClients) {
        if (c.username == username) return c.socket;
    }
    return INVALID_SOCKET;
}

// 服务器广播消息给所有在线客户端
void broadcastServerMessage(const string& message) {
    lock_guard<mutex> lock(clientsMutex);
    string fullMsg = "[BROADCAST] " + message;
    for (auto& client : connectedClients) {
        sendLine(client.socket, fullMsg);
    }
}

// 服务器管理控制台线程，输入广播消息
void serverConsoleThread(HWND hwnd) {
    while (serverRunning) {

        Sleep(100); // 避免CPU占用过高
    }
}


void handleClient(SOCKET clientSock) {
    string username, password;
    bool isAuthenticated = false;



    sendLine(clientSock, "Enter username:");
    //SetWindowTextW(hTextBox, L"1");

    username = recvLine(clientSock);
    
    cout << "Received username: " << username << endl;
    if (username == "<DISCONNECTED>") {
        closesocket(clientSock);
        return;
    }
    
    sendLine(clientSock, "Enter password:");
    password = recvLine(clientSock);
    if (password == "<DISCONNECTED>") {
        closesocket(clientSock);
        return;
    }

    if (userDatabase.find(username) != userDatabase.end() && userDatabase[username] == password) {
        sendLine(clientSock, "Login successful!");
        isAuthenticated = true;
        cout << username << " logged in." << endl;

        {
            lock_guard<mutex> lock(clientsMutex);
            connectedClients.push_back({ clientSock, username });
        }
        printOnlineUsers();
        broadcastOnlineUsers();
    }
    else {
        sendLine(clientSock, "Invalid username or password.");
        closesocket(clientSock);
        return;
    }

    while (isAuthenticated && serverRunning) {
        string input = recvLine(clientSock);
        if (input == "<DISCONNECTED>") break;
        if (input.empty()) continue;

        // 解析命令或消息
        if (input.rfind("CREATE_GROUP:", 0) == 0) {
            string groupName = input.substr(13);
            lock_guard<mutex> lock(groupsMutex);
            if (groups.count(groupName) > 0) {
                sendLine(clientSock, "[ERROR] Group '" + groupName + "' already exists.");
            }
            else {
                groups[groupName] = Group{ groupName, {username} };
                sendLine(clientSock, "[SUCCESS] Group '" + groupName + "' created and you joined.");
            }
        }
        else if (input.rfind("DELETE_GROUP:", 0) == 0) {
            string groupName = input.substr(13);
            lock_guard<mutex> lock(groupsMutex);
            if (groups.count(groupName) == 0) {
                sendLine(clientSock, "[ERROR] Group '" + groupName + "' does not exist.");
            }
            else {
                groups.erase(groupName);
                sendLine(clientSock, "[SUCCESS] Group '" + groupName + "' deleted.");
            }
        }
        else if (input.rfind("JOIN_GROUP:", 0) == 0) {
            string groupName = input.substr(11);
            lock_guard<mutex> lock(groupsMutex);
            if (groups.count(groupName) == 0) {
                sendLine(clientSock, "[ERROR] Group '" + groupName + "' does not exist.");
            }
            else {
                auto& members = groups[groupName].members;
                if (find(members.begin(), members.end(), username) == members.end()) {
                    members.push_back(username);
                    sendLine(clientSock, "[SUCCESS] Joined group '" + groupName + "'.");
                }
                else {
                    sendLine(clientSock, "[ERROR] You are already in group '" + groupName + "'.");
                }
            }
        }
        else if (input.rfind("LEAVE_GROUP:", 0) == 0) {
            string groupName = input.substr(12);
            lock_guard<mutex> lock(groupsMutex);
            if (groups.count(groupName) == 0) {
                sendLine(clientSock, "[ERROR] Group '" + groupName + "' does not exist.");
            }
            else {
                auto& members = groups[groupName].members;
                auto it = find(members.begin(), members.end(), username);
                if (it != members.end()) {
                    members.erase(it);
                    sendLine(clientSock, "[SUCCESS] Left group '" + groupName + "'.");
                    // 若群空自动删除
                    if (members.empty()) {
                        groups.erase(groupName);
                        cout << "Group '" << groupName << "' deleted (no members left)." << endl;
                    }
                }
                else {
                    sendLine(clientSock, "[ERROR] You are not in group '" + groupName + "'.");
                }
            }
        }
        else if (input.rfind("LIST_GROUPS", 0) == 0) {
            lock_guard<mutex> lock(groupsMutex);
            string groupsList = "[GROUP_LIST] ";
            for (auto& g : groups) {
                groupsList += g.first + " ";
            }
            sendLine(clientSock, groupsList);
        }
        else if (input.rfind("LIST_MEMBERS:", 0) == 0) {
            string groupName = input.substr(14);
            sendGroupMembers(clientSock, groupName);
        }
        else if (input.rfind("PRIVATE_MESSAGE:", 0) == 0) {
            string targetUsername = input.substr(16, input.find(" ") - 16);
            string message = input.substr(input.find(" ") + 1);

            // 查找目标用户
            SOCKET targetSocket = getUserSocket(targetUsername);
            if (targetSocket == INVALID_SOCKET) {
                sendLine(clientSock, "[ERROR] User '" + targetUsername + "' is not online.");
            }
            else {
                string privateMessage = "[PRIVATE] From " + username + ": " + message;
                sendLine(targetSocket, privateMessage);  // 直接发送到目标用户的套接字
                sendLine(clientSock, "[PRIVATE] To " + targetUsername + ": " + message);  // 给发送者发送确认
            }
        }
        else if (input.rfind("GROUP_MESSAGE:", 0) == 0) {
            // 提取群组名和消息内容
            string groupName = input.substr(14, input.find(" ") - 14);
            string message = input.substr(input.find(" ") + 1);

            // 查找目标群组
            lock_guard<mutex> lock(groupsMutex);
            auto it = groups.find(groupName);
            if (it == groups.end()) {
                sendLine(clientSock, "[ERROR] Group '" + groupName + "' does not exist.");
            }
            else {
                // 群组存在，广播消息到所有成员
                string fullMessage = "[GROUP] " + groupName + " " + username + ": " + message;

                // 向群组内所有成员发送消息
                for (const string& member : it->second.members) {
                    if (member != username) {  // 不发给自己
                        SOCKET memberSocket = getUserSocket(member);
                        if (memberSocket != INVALID_SOCKET) {
                            sendLine(memberSocket, fullMessage);
                        }
                    }
                }

                // 给自己也发送确认消息
                sendLine(clientSock, "[GROUP] Sent to group '" + groupName + "': " + message);
            }
        }
    }

    // 清理客户端
    {
        lock_guard<mutex> lock(clientsMutex);
        auto it = find_if(connectedClients.begin(), connectedClients.end(), [&](const ClientInfo& c) {
            return c.socket == clientSock;
            });
        if (it != connectedClients.end()) {
            connectedClients.erase(it);
        }
    }
    closesocket(clientSock);
    printOnlineUsers();
    broadcastOnlineUsers();
}

void startServer() {
    WSADATA wsaData;
    SOCKET listenSocket, clientSocket;
    struct sockaddr_in server, client;
    int clientSize = sizeof(client);

    WSAStartup(MAKEWORD(2, 2), &wsaData);
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(8888);

    bind(listenSocket, (struct sockaddr*)&server, sizeof(server));
    listen(listenSocket, 5);

    // 使用 SetWindowTextW 更新文本框内容
    SetWindowTextW(hTextBox, L"Server running on port 8888...");

    //cout << "Server running on port 8888..." << endl;


    while (serverRunning) {
        clientSocket = accept(listenSocket, (struct sockaddr*)&client, &clientSize);
        if (clientSocket == INVALID_SOCKET) {
            cerr << "Accept failed!" << endl;
            continue;
        }
        thread(handleClient, clientSocket).detach();
    }

    closesocket(listenSocket);
    WSACleanup();
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        // 创建文本框
        hTextBox = CreateWindowEx(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            10, 10, 400, 200, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);

        // 创建输入框
        hInputBox = CreateWindowEx(0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE,
            10, 220, 300, 30, hwnd, (HMENU)2, GetModuleHandle(NULL), NULL);

        // 创建发送按钮
        hSendButton = CreateWindowEx(0, L"BUTTON", L"Send",
            WS_CHILD | WS_VISIBLE,
            320, 220, 90, 30, hwnd, (HMENU)3, GetModuleHandle(NULL), NULL);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == 3) {
            // 按钮点击事件
            char buffer[256];
            GetWindowTextA(hInputBox, buffer, 256);  // 获取输入框的内容
            string message = buffer;
            if (!message.empty()) {
                // 发送广播消息
                broadcastServerMessage(message);

                // 使用 std::wstring 进行字符串拼接
                wstring messageToDisplay = L"[SERVER] Broadcasted: ";
                messageToDisplay.append(message.begin(), message.end());  // 将 std::string 转为 std::wstring 并拼接

                appendToTextBox(hTextBox, messageToDisplay);

            }
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

//win32 windows
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {


    // 初始化用户
    initializeUsers();

    // 注册窗口类
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ServerWindowClass";
    RegisterClass(&wc);

    // 创建窗口
    hWnd = CreateWindowEx(0, wc.lpszClassName, L"Server Window",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 450, 300,
        NULL, NULL, hInstance, NULL);

    if (!hWnd) {
        MessageBox(NULL, L"Window creation failed!", L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    // 启动服务器线程
    thread(serverConsoleThread, hWnd).detach();
    thread(startServer).detach();
    //startServer();
   // MessageBox(NULL, L"程序已启动", L"调试", MB_OK);
    cout << "Starting the server..." << endl;

    // 运行消息循环
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}

