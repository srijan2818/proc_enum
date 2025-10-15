#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

using namespace ftxui;

bool isNumeric(const std::string& str) {
    return !str.empty() && std::all_of(str.begin(), str.end(), ::isdigit);
}

std::string pad(const std::string& str, size_t width) {
    if (str.length() >= width)
        return str.substr(0, width);
    return str + std::string(width - str.length(), ' ');
}

int main() {
    auto screen = ScreenInteractive::Fullscreen();

    int scroll_position = 0;
    int rows_per_page = 20; 

    auto renderer = Renderer([&] {
        std::vector<std::vector<std::string>> rows;
        rows.push_back({"PID", "Name", "State", "Threads", "Mem"});

        for (const auto& entry : std::filesystem::directory_iterator("/proc")) {
            if (entry.is_directory()) {
                std::string pid = entry.path().filename();
                if (isNumeric(pid)) {
                    std::string procName = "-", state = "-", vmrss = "-", threads = "-";
                    std::string statusPath = entry.path().string() + "/status";
                    std::ifstream statusFile(statusPath);

                    if (statusFile.is_open()) {
                        std::string line;
                        while (std::getline(statusFile, line)) {
                            if (line.rfind("Name:", 0) == 0) {
                                auto pos = line.find(':');
                                procName = line.substr(pos+1);
                                procName.erase(0, procName.find_first_not_of(" \t"));
                            } else if (line.rfind("VmRSS:", 0) == 0) {
                                auto pos = line.find(':');
                                vmrss = line.substr(pos+1);
                                vmrss.erase(0, vmrss.find_first_not_of(" \t"));
                            } else if (line.rfind("State:", 0) == 0) {
                                auto pos = line.find(':');
                                state = line.substr(pos+1);
                                state.erase(0, state.find_first_not_of(" \t"));
                                auto space_pos = state.find(' ');
                                if (space_pos != std::string::npos)
                                    state = state.substr(0, space_pos);
                            } else if (line.rfind("Threads:", 0) == 0) {
                                auto pos = line.find(':');
                                threads = line.substr(pos+1);
                                threads.erase(0, threads.find_first_not_of(" \t"));
                            }
                        }
                        statusFile.close();
                    }
                    rows.push_back({pid, procName, state, threads, vmrss});
                }
            }
        }

        std::vector<size_t> col_widths = {7, 30, 8, 8, 10};

        int total_rows = rows.size();
        int start_row = std::min(scroll_position, total_rows - rows_per_page);
        int end_row = std::min(start_row + rows_per_page, total_rows);

        Elements table_rows;
        for (int i = start_row; i < end_row; ++i) {
            Elements row_elements;
            for (size_t j = 0; j < rows[i].size(); ++j) {
                std::string row_entry = pad(rows[i][j], col_widths[j]);
                if (i == 0) {
                    row_elements.push_back(text(row_entry) | bold | underlined | center);
                } else {
                    row_elements.push_back(text(row_entry) | center);
                }
            }
            table_rows.push_back(hbox(row_elements));
        }

        auto table_content = vbox(table_rows);

        return vbox({
            text("Process Monitor") | bold | center,
            separator(),
            table_content | vscroll_indicator | frame | flex,
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
