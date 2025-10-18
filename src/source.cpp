#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <sstream>
#include <unordered_map>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>


#include "mem_grph.cpp"
using namespace ftxui;

struct Process {
    std::string pid;
    std::string name;
    std::string state;
    std::string threads;
    std::string vmrss;
    double cpu_percent;
};

bool isNumeric(const std::string& str) {
    return !str.empty() && std::all_of(str.begin(), str.end(), ::isdigit);
}

std::string pad(const std::string& s, size_t width) {
    if (s.size() >= width)
        return s.substr(0, width);
    else
        return s + std::string(width - s.size(), ' ');
}

// --- CPU STAT PARSING ---
std::tuple<uint64_t, uint64_t, uint64_t> parseStat(const std::string& stat_path) {
    std::ifstream statFile(stat_path);
    if (!statFile.is_open()) return {0, 0, 0};

    std::string line;
    std::getline(statFile, line);
    statFile.close();

    size_t first_paren = line.find('(');
    size_t last_paren = line.rfind(')');
    if (first_paren == std::string::npos || last_paren == std::string::npos || last_paren <= first_paren)
        return {0, 0, 0};

    std::string after = line.substr(last_paren + 2);
    std::istringstream iss(after);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) tokens.push_back(token);

    if (tokens.size() < 19)
        return {0, 0, 0};

    uint64_t utime = std::stoull(tokens[10]);
    uint64_t stime = std::stoull(tokens[11]);
    uint64_t starttime = std::stoull(tokens[18]);

    return {utime, stime, starttime};
}

uint64_t getTotalCPUTime() {
    std::ifstream statFile("/proc/stat");
    if (!statFile.is_open()) return 0;
    std::string line;
    std::getline(statFile, line);
    statFile.close();

    std::istringstream iss(line);
    std::string label;
    iss >> label;
    uint64_t total = 0, value;
    while (iss >> value) total += value;
    return total;
}

// --- READ /proc/meminfo ---
std::unordered_map<std::string, long> readMeminfo() {
    std::ifstream f("/proc/meminfo");
    std::unordered_map<std::string, long> info;
    std::string key, unit;
    long value;
    while (f >> key >> value >> unit) {
        if (key.back() == ':') key.pop_back();
        info[key] = value;
    }
    return info;
}

int main() {
    auto screen = ScreenInteractive::Fullscreen();

    int scroll_position = 0;
    const int rows_per_page = 20;
    const int graph_width = 80;

    constexpr size_t pid_w = 7;
    constexpr size_t name_w = 30;
    constexpr size_t state_w = 8;
    constexpr size_t threads_w = 8;
    constexpr size_t mem_w = 10;
    constexpr size_t cpu_w = 7;

    std::vector<int> mem_data;

    struct CpuSnapshot {
        uint64_t total_cpu = 0;
        std::unordered_map<std::string, uint64_t> proc_cpu;
    } prev_snapshot;

    auto renderer = Renderer([&] {
        std::vector<Process> processes;
        uint64_t total_cpu_time = getTotalCPUTime();

        for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
            if (!entry.is_directory()) continue;
            std::string pid = entry.path().filename();
            if (!isNumeric(pid)) continue;

            std::string statusPath = entry.path().string() + "/status";
            std::ifstream statusFile(statusPath);
            if (!statusFile.is_open()) continue;

            std::string line;
            std::string procName = "-", state = "-", vmrss = "0", threads = "-";
            while (std::getline(statusFile, line)) {
                if (line.rfind("Name:", 0) == 0) {
                    procName = line.substr(line.find(':') + 1);
                    procName.erase(0, procName.find_first_not_of(" \t"));
                } else if (line.rfind("VmRSS:", 0) == 0) {
                    vmrss = line.substr(line.find(':') + 1);
                    vmrss.erase(0, vmrss.find_first_not_of(" \t"));
                    vmrss = vmrss.substr(0, vmrss.find(' ')); 
                } else if (line.rfind("State:", 0) == 0) {
                    state = line.substr(line.find(':') + 1);
                    state.erase(0, state.find_first_not_of(" \t"));
                    size_t space_pos = state.find(' ');
                    if (space_pos != std::string::npos)
                        state = state.substr(0, space_pos);
                } else if (line.rfind("Threads:", 0) == 0) {
                    threads = line.substr(line.find(':') + 1);
                    threads.erase(0, threads.find_first_not_of(" \t"));
                }
            }
            statusFile.close();

            auto stat_path = entry.path().string() + "/stat";
            auto [utime, stime, starttime] = parseStat(stat_path);
            uint64_t proc_cpu_total = utime + stime;

            double cpu_percent = 0.0;
            if (prev_snapshot.total_cpu != 0 && total_cpu_time > prev_snapshot.total_cpu) {
                uint64_t proc_prev = prev_snapshot.proc_cpu[pid];
                uint64_t proc_delta = proc_cpu_total > proc_prev ? proc_cpu_total - proc_prev : 0;
                uint64_t total_delta = total_cpu_time - prev_snapshot.total_cpu;
                cpu_percent = 100.0 * proc_delta / total_delta;
            }

            processes.push_back(Process{pid, procName, state, threads, vmrss, cpu_percent});
            prev_snapshot.proc_cpu[pid] = proc_cpu_total;
        }
        prev_snapshot.total_cpu = total_cpu_time;

        std::sort(processes.begin(), processes.end(), [](const Process& a, const Process& b) {
            return a.cpu_percent > b.cpu_percent;
        });

        auto meminfo = readMeminfo();
        long total_kb = meminfo["MemTotal"];
        long available_kb = meminfo["MemAvailable"];
        long used_kb = total_kb - available_kb;
        mem_data.push_back(used_kb);
        if ((int)mem_data.size() > graph_width)
            mem_data.erase(mem_data.begin());

        // --- Table rendering ---
        Elements table_rows;
        Elements header_row;
        header_row.push_back(text(pad("PID", pid_w)) | bold | underlined | center);
        header_row.push_back(text(pad("Name", name_w)) | bold | underlined | center);
        header_row.push_back(text(pad("State", state_w)) | bold | underlined | center);
        header_row.push_back(text(pad("Threads", threads_w)) | bold | underlined | center);
        header_row.push_back(text(pad("Mem(KB)", mem_w)) | bold | underlined | center);
        header_row.push_back(text(pad("CPU%", cpu_w)) | bold | underlined | center);
        table_rows.push_back(hbox(std::move(header_row)));

        int total_rows = processes.size();
        int start_row = std::min(scroll_position, total_rows - rows_per_page);
        int end_row = std::min(start_row + rows_per_page, total_rows);

        for (int i = start_row; i < end_row; ++i) {
            const Process& p = processes[i];
            std::stringstream ss;
            ss.precision(1);
            ss << std::fixed << p.cpu_percent;

            Elements row = {
                text(pad(p.pid, pid_w)) | center,
                text(pad(p.name, name_w)) | center,
                text(pad(p.state, state_w)) | center,
                text(pad(p.threads, threads_w)) | center,
                text(pad(p.vmrss, mem_w)) | center,
                text(pad(ss.str(), cpu_w)) | center
            };
            table_rows.push_back(hbox(std::move(row)));
        }

        auto mem_graph = renderMemoryGraph(mem_data);

        return vbox({
            text("Process Monitor") | bold | center,
            separator(),
            vbox(table_rows) | frame | flex,
            separator(),
            mem_graph |flex,
            text("Use Up/Down arrows to scroll. Press 'q' to quit.") | dim | center
        }) | border;
    });

    auto component = CatchEvent(renderer, [&](Event event) {
        if (event == Event::ArrowUp) {
            scroll_position = std::max(0, scroll_position - 1);
            return true;
        }
        if (event == Event::ArrowDown) {
            scroll_position++;
            return true;
        }
        if (event == Event::Character('q')) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    std::thread refresher([&screen]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            screen.PostEvent(Event::Custom);
        }
    });
    refresher.detach();

    screen.Loop(component);
    return 0;
}
