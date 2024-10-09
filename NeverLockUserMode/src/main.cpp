#include <iostream>
#include <windows.h>
#include <TlHelp32.h>
#include <thread>
#include <chrono>
#include "client_dll.hpp"
#include "offsets.hpp"
#include "buttons.hpp"

// TO DO LIST
// - Make bhop use keystrokes
// - Finish jumpbug
// - Test NULLS
// - Finish trigger bot

static DWORD get_process_id(const wchar_t* process_name)
{
    DWORD process_id = 0;

    HANDLE snap_shot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (snap_shot == INVALID_HANDLE_VALUE)
        return process_id;

    PROCESSENTRY32 entry = {};
    entry.dwSize = sizeof(decltype(entry));

    if (Process32FirstW(snap_shot, &entry) == TRUE)
    {
        if (_wcsicmp(process_name, entry.szExeFile) == 0)
            process_id = entry.th32ProcessID;
        else
        {
            while (Process32NextW(snap_shot, &entry) == TRUE)
            {
                if (_wcsicmp(process_name, entry.szExeFile) == 0)
                {
                    process_id = entry.th32ProcessID;
                    break;
                }
            }
        }
    }

    CloseHandle(snap_shot);
    return process_id;
}

static std::uintptr_t get_module_base(const DWORD pid, const wchar_t* module_name)
{
    std::uintptr_t module_base = 0;

    HANDLE snap_shot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap_shot == INVALID_HANDLE_VALUE)
        return module_base;

    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(decltype(entry));

    if (Module32FirstW(snap_shot, &entry) == TRUE)
    {
        if (wcsstr(module_name, entry.szModule) != nullptr)
            module_base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
        else
        {
            while (Module32NextW(snap_shot, &entry) == TRUE)
            {
                if (wcsstr(module_name, entry.szModule) != nullptr)
                {
                    module_base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
                    break;
                }
            }
        }
    }

    CloseHandle(snap_shot);
    return module_base;
}

// Tried to make safe bhop and jumpbug
void press_spacebar()
{
    // Press spacebar
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_SPACE;  // Virtual key code for spacebar
    SendInput(1, &input, sizeof(INPUT));

    // Immediately release spacebar
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void left_click() {
    // Example of simulating a left mouse click using WinAPI
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &input, sizeof(INPUT)); // Press down
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(INPUT)); // Release
}



namespace driver
{
    namespace codes
    {
        constexpr ULONG attach = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG read = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
        constexpr ULONG write = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x698, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
    }

    struct Request
    {
        HANDLE process_id;
        PVOID target;
        PVOID buffer;
        SIZE_T size;
        SIZE_T return_size;
    };

    bool attach_to_process(HANDLE driver_handle, const DWORD pid)
    {
        Request r;
        r.process_id = reinterpret_cast<HANDLE>(pid);
        return DeviceIoControl(driver_handle, codes::attach, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr);
    }

    template <class T>
    T read_memory(HANDLE driver_handle, const std::uintptr_t addr)
    {
        T temp = {};
        Request r;
        r.target = reinterpret_cast<PVOID>(addr);
        r.buffer = &temp;
        r.size = sizeof(T);
        DeviceIoControl(driver_handle, codes::read, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr);
        return temp;
    }

    template <class T>
    void write_memory(HANDLE driver_handle, const std::uintptr_t addr, const T& value)
    {
        Request r;
        r.target = reinterpret_cast<PVOID>(addr);
        r.buffer = (PVOID)&value;
        r.size = sizeof(T);
        DeviceIoControl(driver_handle, codes::write, &r, sizeof(r), &r, sizeof(r), nullptr, nullptr);
    }
}

int main()
{
    const DWORD pid = get_process_id(L"cs2.exe");

    if (pid == 0)
    {
        std::cout << "Failed to find cs2\n";
        std::cin.get();
        return 1;
    }

    const HANDLE driver = CreateFile(L"\\\\.\\NeverLockKernel", GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (driver == INVALID_HANDLE_VALUE)
    {
        std::cout << "Failed to create driver handle\n";
        std::cin.get();
        return 1;
    }

    if (driver::attach_to_process(driver, pid) == true)
    {
        std::cout << "Attachment successful\n";

        if (const std::uintptr_t client = get_module_base(pid, L"client.dll"); client != 0)
        {
            std::cout << "Client found\n";
            bool safemode = true;
            while (true)
            {
                if (GetAsyncKeyState(VK_END))
                    break;

                const auto local_player_pawn = driver::read_memory<std::uintptr_t>(driver, client + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);

                if (local_player_pawn == 0)
                    continue;

                const auto flags = driver::read_memory<std::uint32_t>(driver, local_player_pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_fFlags);
                const bool in_air = flags & (1 << 0);
                const bool space_pressed = GetAsyncKeyState(VK_SPACE);
                const auto force_jump = driver::read_memory<DWORD>(driver, client + cs2_dumper::buttons::jump);


                if (GetAsyncKeyState(VK_F5))
                {
                    std::cout << "Safe mode disabled\n";
                    safemode = false;
                    Sleep(100);
                }
                if (GetAsyncKeyState(VK_F6))
                {
                    std::cout << "Safe mode enabled\n";
                    safemode = true;
                    Sleep(100);
                }


                // BHOP
                // Memory Writing
                if (safemode == false)
                {
                    if (space_pressed && in_air)
                    {
                        Sleep(10);
                        driver::write_memory(driver, client + cs2_dumper::buttons::jump, 65537);
                    }
                    else if (space_pressed && !in_air)
                    {
                        driver::write_memory(driver, client + cs2_dumper::buttons::jump, 256);
                    }
                    else if (space_pressed && force_jump == 65537)
                    {
                        driver::write_memory(driver, client + cs2_dumper::buttons::jump, 256);
                    }
                }


                // NULLS
                if (!in_air)
                {

                    const bool a_pressed = GetAsyncKeyState('A') & 0x8000;
                    const bool d_pressed = GetAsyncKeyState('D') & 0x8000;

                    INPUT input_a = { 0 };
                    input_a.type = INPUT_KEYBOARD;
                    input_a.ki.wVk = 'A';

                    INPUT input_d = { 0 };
                    input_d.type = INPUT_KEYBOARD;
                    input_d.ki.wVk = 'D';

                    if (a_pressed && !d_pressed)
                    {
                        SendInput(1, &input_a, sizeof(INPUT));

                        input_d.ki.dwFlags = KEYEVENTF_KEYUP;
                        SendInput(1, &input_d, sizeof(INPUT));
                    }
                    else if (d_pressed && !a_pressed)
                    {
                        SendInput(1, &input_d, sizeof(INPUT));

                        input_a.ki.dwFlags = KEYEVENTF_KEYUP;
                        SendInput(1, &input_a, sizeof(INPUT));
                    }
                    else if (!a_pressed && !d_pressed)
                    {
                        input_a.ki.dwFlags = KEYEVENTF_KEYUP;
                        input_d.ki.dwFlags = KEYEVENTF_KEYUP;
                        SendInput(1, &input_a, sizeof(INPUT));
                        SendInput(1, &input_d, sizeof(INPUT));
                    }
                }

                // Jumpbug
                // Memory Writing
                if (safemode == false)
                {
                    const bool mouse5_pressed = GetAsyncKeyState(VK_XBUTTON2);

                    if (mouse5_pressed)
                    {
                        // jumps
                        driver::write_memory(driver, client + cs2_dumper::buttons::jump, 65537);
                        Sleep(5);
                        driver::write_memory(driver, client + cs2_dumper::buttons::jump, 256);
                        // Waits then crouches
                        Sleep(100);
                        driver::write_memory(driver, client + cs2_dumper::buttons::duck, 65537);
                        // checks if player is on the ground if he is then it uncrouches and jumps
                        if (!in_air)
                        {
                            driver::write_memory(driver, client + cs2_dumper::buttons::duck, 256);
                            driver::write_memory(driver, client + cs2_dumper::buttons::jump, 65537);
                            Sleep(10);
                            driver::write_memory(driver, client + cs2_dumper::buttons::jump, 256);
                        }



                    }
                }

                // Triger Bot
                const bool mmb_pressed = GetAsyncKeyState(VK_F1);
                if (mmb_pressed)
                {

                    const auto entity_list = driver::read_memory<std::uint32_t>(driver, client + cs2_dumper::offsets::client_dll::dwEntityList);

                    for (int i = 0; i < 64; i++)
                    {
                        const auto crossair_entity_index = driver::read_memory<int>(driver, local_player_pawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_iIDEntIndex);
                        if (crossair_entity_index < 0 || !crossair_entity_index)
                        {
                            std::cout << "Crosshair Entity Index: " << crossair_entity_index << std::endl;
                            continue;
                        }

                        std::uintptr_t list_entry = driver::read_memory<std::uintptr_t>(driver, entity_list + (8 * (i & 0x7FFF) >> 9) + 16);
                        if (!list_entry)
                        {
                            std::cout << "List Entry Address: " << std::hex << list_entry << std::endl;
                            continue;
                        }

                        const auto entity = driver::read_memory<std::uintptr_t>(driver, list_entry + 120 * (i & 0x1FF));
                        if (!entity)
                        {
                            std::cout << "Entity Address: " << std::hex << entity << std::endl;
                            continue;
                        }

                        left_click();
                        std::cout << "shot" << std::endl;
                        Sleep(100); // Sleep for 10 milliseconds

                    }
                }

                Sleep(1);
                /*std::this_thread::sleep_for(std::chrono::milliseconds(1));*/
            }

        }
    }

    CloseHandle(driver);
    std::cin.get();
    return 0;
}
