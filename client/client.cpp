#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>      
#include <ws2tcpip.h>     
#include <iostream>
#include <string>
#include <thread>         
#include <fstream>
#include <cstring>
#include <vector>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")   // Подключаем библиотеку Winsock

using namespace std;

// Настройки подключения
const char* SERVER_IP = "127.0.0.1";  
const int PORT = 12345;              
const int BUF_SIZE = 4096;           

SOCKET clientSock;                   // Глобальный сокет клиента

// Функция для фонового потока: постоянно читает сообщения от сервера
void ReceiveThread() {
    char buffer[BUF_SIZE];
    while (true) {
        int bytes = recv(clientSock, buffer, BUF_SIZE - 1, 0);
        if (bytes <= 0) {
            cout << "\n[Сервер отключился]" << endl;
            break;
        }
        buffer[bytes] = '\0';
        // Игнорируем служебное сообщение "NAME" (осталось от старой версии)
        if (strcmp(buffer, "NAME") != 0) {
            cout << "\n" << buffer << "\n> ";
            cout.flush();
        }
    }
}

// Отправить файл на сервер
void SendFile(const string& filepath) {
    // Извлекаем только имя файла из полного пути
    string filename = filepath;
    size_t pos = filename.find_last_of("\\/");
    if (pos != string::npos) filename = filename.substr(pos + 1);

    // Открываем локальный файл для чтения
    HANDLE hFile = CreateFileA(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        cout << "Ошибка открытия файла" << endl;
        return;
    }
    DWORD fileSize = GetFileSize(hFile, NULL);   // Узнаём размер файла

    // Отправляем команду "sendfile|имя_файла"
    string cmd = "/sendfile " + filename;
    send(clientSock, cmd.c_str(), cmd.length() + 1, 0);
    // Отправляем размер файла (чтобы сервер знал, сколько читать)
    send(clientSock, (char*)&fileSize, sizeof(fileSize), 0);

    // Читаем файл блоками и отправляем на сервер
    char buf[BUF_SIZE];
    DWORD br;
    while (ReadFile(hFile, buf, BUF_SIZE, &br, NULL) && br > 0) {
        send(clientSock, buf, br, 0);
    }
    CloseHandle(hFile);

    // Ждём подтверждения от сервера
    char resp[BUF_SIZE];
    int bytes = recv(clientSock, resp, BUF_SIZE - 1, 0);
    if (bytes > 0) {
        resp[bytes] = '\0';
        cout << "[Сервер] " << resp << endl; 
    }
}

// Скачать файл с сервера
void GetFile(const string& filename) {
    string cmd = "/getfile " + filename;
    send(clientSock, cmd.c_str(), cmd.length() + 1, 0);

    // Сервер сначала присылает размер файла (4 байта)
    DWORD fileSize;
    int bytes = recv(clientSock, (char*)&fileSize, sizeof(fileSize), 0);
    if (bytes != sizeof(fileSize)) {
        cout << "Ошибка получения размера файла" << endl;
        return;
    }
    if (fileSize == 0) {   // Файл не найден
        char err[BUF_SIZE];
        recv(clientSock, err, BUF_SIZE - 1, 0);
        cout << "[Сервер] " << err << endl;
        return;
    }

    cout << "Скачивание " << filename << " (" << fileSize << " байт)..." << endl;
    // Создаём локальный файл для записи
    HANDLE hFile = CreateFileA(filename.c_str(), GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        cout << "Ошибка создания файла" << endl;
        return;
    }

    char buf[BUF_SIZE];
    DWORD total = 0;
    while (total < fileSize) {
        DWORD toRead = min((DWORD)BUF_SIZE, fileSize - total);
        bytes = recv(clientSock, buf, toRead, 0);
        if (bytes <= 0) break;
        DWORD bw;
        WriteFile(hFile, buf, bytes, &bw, NULL);
        total += bytes;
    }
    CloseHandle(hFile);
    cout << "Файл сохранён: " << filename << endl;
}

// Вывод справки
void ShowHelp() {
    cout << "\n=== КОМАНДЫ ===\n"
        << " <текст>            - отправить сообщение в свою комнату\n"
        << " /room <название>   - сменить комнату\n"
        << " /spaces <файл>     - подсчитать пробелы в файле на сервере\n"
        << " /sendfile <путь>   - отправить файл на сервер\n"
        << " /getfile <имя>     - скачать файл с сервера\n"
        << " /save <комната>    - сохранить историю комнаты в файл на сервере\n"
        << " /view <комната>    - показать сохранённую историю\n"
        << " /broad <сообщение> - отправить всем клиентам\n"
        << " /help              - справка\n"
        << " /exit              - выход\n"
        << "===================\n";
}

int main() {
    // Устанавливаем кодировку для корректного отображения кириллицы
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);

    // Инициализация Winsock (обязательный шаг для всех сетевых приложений Windows)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "Ошибка WinSock" << endl;
        return 1;
    }

    // Создание сокета (AF_INET - IPv4, SOCK_STREAM - TCP)
    clientSock = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSock == INVALID_SOCKET) {
        cout << "Ошибка socket" << endl;
        WSACleanup();
        return 1;
    }

    // Настройка адреса сервера
    sockaddr_in servAddr;
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(PORT);                 // порт в сетевом порядке байт
    inet_pton(AF_INET, SERVER_IP, &servAddr.sin_addr); // преобразует строку IP в двоичный вид

    // Подключение к серверу
    if (connect(clientSock, (sockaddr*)&servAddr, sizeof(servAddr)) == SOCKET_ERROR) {
        cout << "Ошибка подключения к серверу" << endl;
        closesocket(clientSock);
        WSACleanup();
        return 1;
    }
    cout << "Подключено к серверу!" << endl;

    // Сервер может запросить имя клиента — отправляем "Anonymous"
    char nameReq[5] = { 0 };
    recv(clientSock, nameReq, 4, 0);
    if (strcmp(nameReq, "NAME") == 0) {
        string name;
        cout << "Введите ваше имя: ";
        getline(cin, name);
        // Удаляем лишние пробелы
        name.erase(0, name.find_first_not_of(" \t\r\n"));
        name.erase(name.find_last_not_of(" \t\r\n") + 1);
        if (name.empty()) name = "Anonymous";
        send(clientSock, name.c_str(), name.length() + 1, 0);
    }

    // Запускаем отдельный поток для чтения входящих сообщений от сервера (не блокирует главный цикл)
    thread recvThread(ReceiveThread);
    recvThread.detach();   // поток работает независимо

    ShowHelp();

    // Главный цикл обработки команд пользователя
    string input;
    while (true) {
        cout << "> ";
        getline(cin, input);
        // Удаляем пробелы в начале и конце строки
        input.erase(0, input.find_first_not_of(" \t\r\n"));
        input.erase(input.find_last_not_of(" \t\r\n") + 1);
        if (input.empty()) continue;

        if (input == "/exit") {
            send(clientSock, "/exit", 6, 0);
            break;
        }
        else if (input.rfind("/sendfile ", 0) == 0) {   // команда отправки файла
            string path = input.substr(10);
            SendFile(path);
        }
        else if (input.rfind("/getfile ", 0) == 0) {    // команда получения файла
            string fname = input.substr(9);
            GetFile(fname);
        }
        else if (input.rfind("/help", 0) == 0) {
            ShowHelp();
        }
        else {
            // Любые другие строки (сообщения, команды вроде /room, /spaces, /broad и т.д.)
            // просто отправляем серверу — он сам разберётся
            send(clientSock, input.c_str(), input.length() + 1, 0);
        }
    }

    // 8. Завершение работы: закрываем сокет и очищаем Winsock
    closesocket(clientSock);
    WSACleanup();
    return 0;
}



///sendfile C:\Users\234873\Desktop\привет
