#include <ftxui/dom/elements.hpp>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <sstream>

using namespace ftxui;

ftxui::Element renderMemoryGraph(const std::vector<int>& memory_samples) {
    if (memory_samples.empty())
        return text("No memory data yet.") | center;

    const int graph_height = 15;
    const int graph_width = 80;
    std::ifstream f("/proc/meminfo");
    long mem_total_kb = 0;
    std::string key;
    long value;
    std::string unit;
    while (f >> key >> value >> unit) {
        if (key == "MemTotal:") {
            mem_total_kb = value;
            break;
        }
    }

    int start = std::max(0, (int)memory_samples.size() - graph_width);
    std::vector<Element> columns;

    long max_val = *std::max_element(memory_samples.begin(), memory_samples.end());
    long denom = mem_total_kb > 0 ? mem_total_kb : max_val;

    for (int i = start; i < (int)memory_samples.size(); ++i) {
        double ratio = (double)memory_samples[i] / denom;
        ratio = std::clamp(ratio, 0.0, 1.0);

        int bar_height = (int)(ratio * graph_height * 3.0);
        if (bar_height < 1) bar_height = 1;
        if (bar_height > graph_height) bar_height = graph_height;

        Color color = Color::Green;
        if (ratio >= 0.75) color = Color::Red;
        else if (ratio >= 0.5) color = Color::Yellow;

        std::vector<Element> col;
        for (int r = 0; r < graph_height - bar_height; ++r)
            col.push_back(text(" "));
        for (int r = 0; r < bar_height; ++r)
            col.push_back(text("█") | ftxui::color(color));

        columns.push_back(vbox(col));
    }

    long current_kb = memory_samples.back();
    long peak_kb = max_val;
    int usage_percent = denom ? (int)((100.0 * current_kb) / denom + 0.5) : 0;

    return vbox({
        text("Memory Usage Over Time") | bold | center,
        hbox(columns) | border | flex,
        hbox({
            text("Total: " + std::to_string(mem_total_kb / 1024) + " MB") | dim,
            filler(),
            text("Peak: " + std::to_string(peak_kb / 1024) + " MB") | dim,
            filler(),
            text("Current: " + std::to_string(current_kb / 1024) + " MB") | bold,
            filler(),
            text("Usage: " + std::to_string(usage_percent) + "%") | dim
        }) | border
    });
}
