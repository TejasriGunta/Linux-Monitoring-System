#include "../include/monitor.h"
#include <ncurses.h>
#include <thread>
#include <chrono>
#include <signal.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <sstream>

// Helper to cast void* windows in header back to WINDOW*
static WINDOW* toWin(void* p) { return static_cast<WINDOW*>(p); }

void ActivityMonitor::initializeWindows() {
    initscr();
    start_color();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_YELLOW, COLOR_BLACK);
    init_pair(3, COLOR_RED, COLOR_BLACK);
    init_pair(4, COLOR_CYAN, COLOR_BLACK);
    init_pair(5, COLOR_WHITE, COLOR_BLUE);
    init_pair(6, COLOR_BLUE, COLOR_BLACK);
    init_pair(7, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(8, COLOR_CYAN, COLOR_BLACK);
    init_pair(9, COLOR_YELLOW, COLOR_BLACK);
    init_pair(10, COLOR_RED, COLOR_BLACK);
    init_pair(11, COLOR_GREEN, COLOR_BLACK);
    init_pair(12, COLOR_WHITE, COLOR_BLACK);
    init_pair(13, COLOR_BLUE, COLOR_BLACK);

    // detect 256-color support
    use_256_colors = (COLORS >= 256);

    getmaxyx(stdscr, terminal_height, terminal_width);

    int margin = 1;
    int content_w = terminal_width - margin * 2;

    // Layout matching user diagram:
    // Row 1: CPU (full width) with legend on right
    // Row 2: System Info (left ~40%) | Disk (right ~60%)
    // Row 3: Process (left ~60%) | Memory + Network stacked (right ~40%)
    
    int cpu_h = std::max(6, terminal_height / 4); // Reduced CPU height to give more to memory/network
    int mid_h = std::max(8, (terminal_height - cpu_h) / 3); // Increased for system info spacing
    int bottom_h = terminal_height - cpu_h - mid_h - 2;

    // Middle row split: System Info left, Disk right
    int sysinfo_w = std::max(20, (content_w * 4) / 10); // 40% for system info
    int disk_w = content_w - sysinfo_w - 1;

    // Bottom row split: Process left, Memory+Network right
    int process_w = std::max(30, (content_w * 6) / 10); // 60% for processes
    int right_col_w = content_w - process_w - 1;
    
    // Split bottom right into Memory (top) and Network (bottom)
    int mem_h = std::max(5, bottom_h / 2);
    int network_h = bottom_h - mem_h - 1;

    cpu_win = newwin(cpu_h, content_w, 0, margin);
    sysinfo_win = newwin(mid_h, sysinfo_w, cpu_h, margin);
    disk_win = newwin(mid_h, disk_w, cpu_h, margin + sysinfo_w + 1);
    process_win = newwin(bottom_h, process_w, cpu_h + mid_h, margin);
    mem_win = newwin(mem_h, right_col_w, cpu_h + mid_h, margin + process_w + 1);
    network_win = newwin(network_h, right_col_w, cpu_h + mid_h + mem_h + 1, margin + process_w + 1);
}

void ActivityMonitor::resizeWindows() {
    int h, w;
    getmaxyx(stdscr, h, w);
    if (h == terminal_height && w == terminal_width) return;

    terminal_height = h;
    terminal_width = w;

    if (sysinfo_win) delwin(toWin(sysinfo_win));
    if (cpu_win) delwin(toWin(cpu_win));
    if (mem_win) delwin(toWin(mem_win));
    if (disk_win) delwin(toWin(disk_win));
    if (network_win) delwin(toWin(network_win));
    if (process_win) delwin(toWin(process_win));

    initializeWindows();
}

static void drawHeader(WINDOW* w, const char* title) {
    box(w, 0, 0);
    wattron(w, COLOR_PAIR(5));
    mvwprintw(w, 0, 2, " %s ", title);
    wattroff(w, COLOR_PAIR(5));
}

// ========================= CPU PANEL =========================
void ActivityMonitor::displayCPUInfo() {
    WINDOW* w = toWin(cpu_win);
    werase(w);
    drawHeader(w, "CPU Usage");

    int h, wid;
    getmaxyx(w, h, wid);
    (void)h;

    int legend_w = 20;
    int legend_x = std::max(2, wid - legend_w - 2);
    int graph_x = 2;
    int graph_w = std::max(10, legend_x - graph_x - 2);
    int graph_h = std::max(4, h - 4);
    int graph_base = 1;

    int logical_cores = (int)cpu_info.core_usage.size();
    int cores_avail = logical_cores;
    (void)cores_avail; // silence unused variable warning
    int legend_count = 0;

    // Optionally aggregate logical CPUs into physical cores by pairing
    bool use_physical = config.aggregate_physical && (logical_cores >= 2) && (logical_cores % 2 == 0);
    int physical_cores = use_physical ? (logical_cores / 2) : logical_cores;
    legend_count = std::min(8, physical_cores);
    (void)legend_count; // silence unused-variable when legend not used

    // Prepare per-display-core current usage and histories (aggregate if requested)
    std::vector<float> display_core_usage;
    std::vector<std::vector<float>> display_cpu_history;
    if (use_physical) {
        display_core_usage.assign(physical_cores, 0.0f);
        display_cpu_history.assign(physical_cores, std::vector<float>());
        for (int p = 0; p < physical_cores; ++p) {
            int a = p * 2;
            int b = a + 1;
            float ua = (a < logical_cores) ? cpu_info.core_usage[a] : 0.0f;
            float ub = (b < logical_cores) ? cpu_info.core_usage[b] : 0.0f;
            display_core_usage[p] = (ua + ub) / 2.0f;
            // build aggregated history by averaging corresponding samples
            static const std::vector<float> empty_vec;
            const std::vector<float> &ha = (a < (int)cpu_history.size()) ? cpu_history[a] : empty_vec;
            const std::vector<float> &hb = (b < (int)cpu_history.size()) ? cpu_history[b] : empty_vec;
            size_t maxlen = std::max(ha.size(), hb.size());
            display_cpu_history[p].reserve(maxlen);
            for (size_t j = 0; j < maxlen; ++j) {
                float va = (j < ha.size()) ? ha[j] : 0.0f;
                float vb = (j < hb.size()) ? hb[j] : 0.0f;
                display_cpu_history[p].push_back((va + vb) / 2.0f);
            }
        }
    } else {
        display_core_usage = cpu_info.core_usage;
        display_cpu_history = cpu_history;
    }

    // Show all CPUs/cores (user requested all cores visible)
    int top_n = (int)display_core_usage.size(); // show all
    std::vector<std::pair<float,int>> usage_idx;
    for (int i = 0; i < (int)display_core_usage.size(); ++i) usage_idx.emplace_back(display_core_usage[i], i);
    std::sort(usage_idx.begin(), usage_idx.end(), std::greater<>());
    std::vector<int> plot_cores;
    for (int i = 0; i < (int)usage_idx.size() && (int)plot_cores.size() < top_n; ++i) plot_cores.push_back(usage_idx[i].second);

    // Draw boxed legend at top-right sorted by usage
    int lox = legend_x;
    int loy = 1;
    int lg_h = std::max(3, (int)plot_cores.size() + 2);
    int lg_w = std::min(legend_w, wid - lox - 1);
    if (lg_w > 10 && !plot_cores.empty()) {
        WINDOW* lg = derwin(w, lg_h, lg_w, 0, lox);
        box(lg, 0, 0);
        wattron(lg, COLOR_PAIR(5)); mvwprintw(lg, 0, 2, " CPUs "); wattroff(lg, COLOR_PAIR(5));
        for (int i = 0; i < (int)plot_cores.size(); ++i) {
            int idx = plot_cores[i];
            int colpair = 6 + (idx % 8);
            float cur = display_core_usage[idx];
            wattron(lg, COLOR_PAIR(colpair) | A_BOLD);
                mvwaddch(lg, 1 + i, 1, ACS_BULLET);
            wattroff(lg, COLOR_PAIR(colpair) | A_BOLD);
            // label physical cores as Pn when aggregated, otherwise CPU
            if (use_physical) mvwprintw(lg, 1 + i, 3, "P%-2d %5.1f%%", idx, cur);
            else mvwprintw(lg, 1 + i, 3, "CPU%-2d %5.1f%%", idx, cur);
        }
    mvwprintw(lg, lg_h-1, 3, "Total: %5.1f%%", cpu_info.total_usage);
        wrefresh(lg);
        delwin(lg);
    } else {
        mvwprintw(w, loy, graph_x + 12, "Total: %5.1f%%", cpu_info.total_usage);
    }

    const float plot_threshold = 0.5f;
    if (cpu_info.total_usage < plot_threshold) {
        mvwprintw(w, graph_base + graph_h / 2,
                  graph_x + std::max(0, graph_w / 2 - 4), "Idle");
        wrefresh(w);
        return;
    }

    // Clear graph area (we draw only per-core dots for a clean dot-dot graph)
    for (int gy = 0; gy < graph_h; ++gy)
        for (int gx = 0; gx < graph_w; ++gx)
            mvwaddch(w, graph_base + gy, graph_x + gx, ' ');

    int total_len = (int)total_history.size();
    int start_total = std::max(0, total_len - graph_w);
    (void)start_total; // suppress unused warning (kept for future use)

        // Plot all cores; use absolute positioning so past points stay fixed
        for (int pi = 0; pi < (int)plot_cores.size(); ++pi) {
            int c = plot_cores[pi];
            int col = 6 + (c % 8);
            static const std::vector<float> empty_vec2;
            const std::vector<float>& hist = (c < (int)display_cpu_history.size()) ? display_cpu_history[c] : empty_vec2;
            if (hist.empty()) continue;
            
            // Map latest history samples to rightmost columns; older samples stay in place
            int hist_len = (int)hist.size();
            int samples_to_draw = std::min(graph_w, hist_len);
            
            for (int x = 0; x < samples_to_draw; ++x) {
                // Map column x to absolute history index (latest samples on right)
                int hist_idx = hist_len - samples_to_draw + x;
                if (hist_idx < 0 || hist_idx >= hist_len) continue;
                
                float val = hist[hist_idx]; // raw sample
                if (val < 0.1f) continue; // skip near-zero values
                
                int level = static_cast<int>(val / 100.0f * (graph_h - 1) + 0.5f);
                int row = graph_base + (graph_h - 1 - level);
                wattron(w, COLOR_PAIR(col) | A_BOLD);
                mvwaddch(w, row, graph_x + x, ACS_BULLET);
                wattroff(w, COLOR_PAIR(col) | A_BOLD);
            }
        }

    // (no total overlay) — keep graph as simple colored dots only

    wrefresh(w);
}



// ========================= MEMORY PANEL =========================
void ActivityMonitor::displayMemoryInfo() {
    WINDOW* w = toWin(mem_win);
    werase(w);
    drawHeader(w, "Memory Usage");
    int h, wid;
    getmaxyx(w, h, wid);
    (void)h;

    // Print numeric summaries with color coding matching graph
    wattron(w, COLOR_PAIR(4)); // cyan for Main
    mvwprintw(w, 1, 2, "Main %3.0f%%", memory_info.percent_used); 
    wattroff(w, COLOR_PAIR(4));
    
    wattron(w, COLOR_PAIR(2)); // bright yellow for Swap
    mvwprintw(w, 2, 2, "Swap %3.0f%%", memory_info.swap_percent_used); 
    wattroff(w, COLOR_PAIR(2));

    // Draw continuous smooth line graph like the reference image
    int graph_y = 4;
    int graph_h = std::max(3, h - graph_y - 1);
    int graph_w = std::max(10, wid - 6);
    
    // Use absolute positioning - map latest samples to rightmost columns
    int mem_len = (int)mem_history.size();
    int swap_len = (int)swap_history.size();
    int samples = std::min(graph_w, std::max(mem_len, swap_len));

    // clear graph area
    for (int gy = 0; gy < graph_h; ++gy)
        for (int gx = 0; gx < graph_w; ++gx)
            mvwaddch(w, graph_y + gy, 2 + gx, ' ');

    // Find max value to scale graph properly so both lines are visible
    float max_val = 10.0f; // minimum scale
    for (int x = 0; x < samples; ++x) {
        int mem_idx = mem_len - samples + x;
        int swap_idx = swap_len - samples + x;
        if (mem_idx >= 0 && mem_idx < mem_len) {
            if (mem_history[mem_idx] > max_val) max_val = mem_history[mem_idx];
        }
        if (swap_idx >= 0 && swap_idx < swap_len) {
            if (swap_history[swap_idx] > max_val) max_val = swap_history[swap_idx];
        }
    }
    if (max_val < 10.0f) max_val = 10.0f;
    if (max_val > 100.0f) max_val = 100.0f;

    // Draw continuous lines with dots
    for (int x = 0; x < samples; ++x) {
        int mem_idx = mem_len - samples + x;
        int swap_idx = swap_len - samples + x;
        
        // Main memory line (cyan dots)
        if (mem_idx >= 0 && mem_idx < mem_len) {
            float mv = mem_history[mem_idx];
            int mlevel = static_cast<int>((mv / max_val) * (graph_h - 1) + 0.5f);
            if (mlevel >= graph_h) mlevel = graph_h - 1;
            int mrow = graph_y + (graph_h - 1 - mlevel);
            wattron(w, COLOR_PAIR(4) | A_BOLD); // cyan
            mvwaddch(w, mrow, 2 + x, ACS_BULLET);
            wattroff(w, COLOR_PAIR(4) | A_BOLD);
        }

        // Swap memory line (yellow dots)
        if (swap_idx >= 0 && swap_idx < swap_len) {
            float sv = swap_history[swap_idx];
            int slevel = static_cast<int>((sv / max_val) * (graph_h - 1) + 0.5f);
            if (slevel >= graph_h) slevel = graph_h - 1;
            int srow = graph_y + (graph_h - 1 - slevel);
            wattron(w, COLOR_PAIR(2) | A_BOLD); // yellow
            mvwaddch(w, srow, 2 + x, ACS_BULLET); // Changed from ACS_DIAMOND to ACS_BULLET
            wattroff(w, COLOR_PAIR(2) | A_BOLD);
        }
    }
    wrefresh(w);
}

// ========================= DISK PANEL =========================
void ActivityMonitor::displayDiskInfo() {
    WINDOW* w = toWin(disk_win);
    werase(w);
    drawHeader(w, "Disk Usage");
    int h, wid;
    getmaxyx(w, h, wid);
    // responsive column widths based on window width
    int col1 = std::min(20, std::max(8, wid / 6));
    int col2 = std::min(30, std::max(10, wid / 3));
    int rem = wid - 6 - col1 - col2; if (rem < 20) rem = 20;
    int col3 = rem / 2;
    int col4 = rem - col3;

    mvwprintw(w, 1, 2, "%-*s %-*s %*s %*s", col1, "Disk", col2, "Mount", col3, "Used", col4, "Free");
    int row = 2;
    for (const auto& d : disk_info) {
        if (row >= h - 1) break;
        unsigned long long used = d.used_space;
        std::string dev = d.device;
        std::string mnt = d.mount_point;
        if ((int)dev.size() > col1) dev = dev.substr(0, col1-3) + "...";
        if ((int)mnt.size() > col2) mnt = mnt.substr(0, col2-3) + "...";
        std::string used_s = formatSize(used);
        std::string free_s = formatSize(d.free_space);
        mvwprintw(w, row, 2, "%-*s %-*s %*s %*s", col1, dev.c_str(), col2, mnt.c_str(), col3, used_s.c_str(), col4, free_s.c_str());
        row++;
    }
    wrefresh(w);
}

// ========================= NETWORK PANEL =========================
void ActivityMonitor::displayNetworkInfo() {
    WINDOW* w = toWin(network_win);
    werase(w);
    drawHeader(w, "Network Usage");
    int h, wid;
    getmaxyx(w, h, wid);
    (void)h;

    // numeric summary - show current rate and session totals
    float rx_kbps = (net_rx_history.empty() ? 0.0f : net_rx_history.back());
    float tx_kbps = (net_tx_history.empty() ? 0.0f : net_tx_history.back());

    double session_rx_mb = 0.0;
    double session_tx_mb = 0.0;
    if (curr_net_rx_bytes >= net_start_rx) session_rx_mb = (double)(curr_net_rx_bytes - net_start_rx) / (1024.0 * 1024.0);
    if (curr_net_tx_bytes >= net_start_tx) session_tx_mb = (double)(curr_net_tx_bytes - net_start_tx) / (1024.0 * 1024.0);

    mvwprintw(w, 1, 2, "Total Rx: %6.2f MB", session_rx_mb);
    mvwprintw(w, 2, 2, "Rx/s:     %7.1f KB/s", rx_kbps);
    mvwprintw(w, 4, 2, "Total Tx: %6.2f MB", session_tx_mb);
    mvwprintw(w, 5, 2, "Tx/s:     %7.1f KB/s", tx_kbps);

    // Draw simple horizontal filled bars (compact layout)
    int bar_y_rx = 3; // place right after numeric summary
    int bar_y_tx = 6; // a bit lower for TX
    int bar_w = std::max(20, wid - 4);
    
    // Find max for scaling
    float max_kbps = 10.0f;
    for (float v : net_rx_history) if (v > max_kbps) max_kbps = v;
    for (float v : net_tx_history) if (v > max_kbps) max_kbps = v;
    
    // Compute fill widths
    float rx_pct = std::min(100.0f, (rx_kbps / max_kbps) * 100.0f);
    float tx_pct = std::min(100.0f, (tx_kbps / max_kbps) * 100.0f);
    int rx_fill = static_cast<int>((bar_w * rx_pct / 100.0f) + 0.5f);
    int tx_fill = static_cast<int>((bar_w * tx_pct / 100.0f) + 0.5f);
    
    // Draw RX bar (cyan filled)
    for (int x = 0; x < bar_w; ++x) {
        if (x < rx_fill) {
            wattron(w, COLOR_PAIR(4) | A_BOLD);
            mvwaddch(w, bar_y_rx, 2 + x, ACS_CKBOARD);
            wattroff(w, COLOR_PAIR(4) | A_BOLD);
        } else {
            mvwaddch(w, bar_y_rx, 2 + x, ' ');
        }
    }
    
    // Draw TX bar (red filled)
    for (int x = 0; x < bar_w; ++x) {
        if (x < tx_fill) {
            wattron(w, COLOR_PAIR(10) | A_BOLD);
            mvwaddch(w, bar_y_tx, 2 + x, ACS_CKBOARD);
            wattroff(w, COLOR_PAIR(10) | A_BOLD);
        } else {
            mvwaddch(w, bar_y_tx, 2 + x, ' ');
        }
    }

    wrefresh(w);
}

// NOTE: Duplicate/legacy network panel implementation removed.
// The active network renderer is the earlier displayNetworkInfo() which
// uses the single `network_win` window pointer. This prevents inconsistencies
// where some code referenced a non-existent `net_win` field.

// ========================= TEMPERATURE PANEL =========================
// ========================= SYSTEM INFO PANEL =========================
void ActivityMonitor::displaySystemInfo() {
    WINDOW* w = toWin(sysinfo_win);
    if (!w) return;
    werase(w);
    drawHeader(w, "System Info");

    int h, wid;
    getmaxyx(w, h, wid);
    (void)h;
    
    // Format uptime
    int days = (int)(system_info.uptime_seconds / 86400);
    int hours = (int)((system_info.uptime_seconds - days * 86400) / 3600);
    int mins = (int)((system_info.uptime_seconds - days * 86400 - hours * 3600) / 60);
    
    char uptime_str[64];
    if (days > 0) {
        snprintf(uptime_str, sizeof(uptime_str), "%dd %dh %dm", days, hours, mins);
    } else if (hours > 0) {
        snprintf(uptime_str, sizeof(uptime_str), "%dh %dm", hours, mins);
    } else {
        snprintf(uptime_str, sizeof(uptime_str), "%dm", mins);
    }

    // Determine load color based on number of cores
    int cores = cpu_info.num_cores;
    if (cores == 0) cores = 1;
    
    auto getLoadColor = [cores](float load) -> int {
        float load_per_core = load / cores;
        if (load_per_core >= 1.0f) return 3; // red (overloaded)
        if (load_per_core >= 0.7f) return 2; // yellow (high)
        return 1; // green (normal)
    };
    
    int load_color_1 = getLoadColor(system_info.load_1min);
    int load_color_5 = getLoadColor(system_info.load_5min);
    int load_color_15 = getLoadColor(system_info.load_15min);

    // Format rate helper
    auto formatRate = [](float rate) -> std::string {
        if (rate >= 1000000) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1fM/s", rate / 1000000.0f);
            return buf;
        } else if (rate >= 1000) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.1fK/s", rate / 1000.0f);
            return buf;
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0f/s", rate);
            return buf;
        }
    };

    // Line 1: Uptime
    mvwprintw(w, 1, 2, "Uptime: %s", uptime_str);
    
    // Line 2: Load (1m)
    mvwprintw(w, 2, 2, "Load (1m): ");
    wattron(w, COLOR_PAIR(load_color_1));
    wprintw(w, "%.2f", system_info.load_1min);
    wattroff(w, COLOR_PAIR(load_color_1));

    // Line 3: Interrupts
    mvwprintw(w, 3, 2, "Interrupts: ");
    wattron(w, COLOR_PAIR(9)); // yellow
    wprintw(w, "%s", formatRate(system_info.interrupts_per_sec).c_str());
    wattroff(w, COLOR_PAIR(9));

    // Line 4: Context switches
    mvwprintw(w, 4, 2, "Context Switches: ");
    wattron(w, COLOR_PAIR(4)); // cyan
    wprintw(w, "%s", formatRate(system_info.ctx_switches_per_sec).c_str());
    wattroff(w, COLOR_PAIR(4));

    wrefresh(w);
}

// ========================= PROCESS PANEL =========================
void ActivityMonitor::displayProcessInfo() {
    WINDOW* w = toWin(process_win);
    werase(w);
    drawHeader(w, "Processes (q=quit, k=kill, r=refresh)");

    int h, wid;
    getmaxyx(w, h, wid);
    mvwprintw(w, 1, 2, "%-6s %-25s %-8s %-8s", "PID", "Name", "CPU%", "Mem%");

    int rows = h - 5;
    int index = process_list_offset;

    for (int i = 0; i < rows && index < (int)processes.size(); ++i, ++index) {
        const Process& p = processes[index];
        int abs_idx = index;
        if (abs_idx == process_selected) wattron(w, A_REVERSE);
        mvwprintw(w, i + 2, 2, "%-6d %-25s %7.1f %7.1f",
                  p.pid, p.name.c_str(), p.cpu_percent, p.mem_percent);
        if (abs_idx == process_selected) wattroff(w, A_REVERSE);
    }

    if ((int)processes.size() > rows) {
        mvwprintw(w, h - 1, wid - 20, "Showing %d/%zu", rows, processes.size());
    }
    wrefresh(w);
}

// ========================= ALERT PANEL =========================
void ActivityMonitor::displayAlert() {
    if (!config.show_alert) return;
    if (cpu_info.total_usage <= config.cpu_threshold) return;
    int y = 0;
    int x = terminal_width - 40;
    mvprintw(y, x, "!!! CPU USAGE HIGH: %.1f%% !!!", cpu_info.total_usage);
    refresh();
}

// ========================= CONFIRMATION DIALOG =========================
bool ActivityMonitor::displayConfirmationDialog(const std::string& message) {
    int h = 7, w = 60;
    int sy = (terminal_height - h) / 2;
    int sx = (terminal_width - w) / 2;

    WINDOW* d = newwin(h, w, sy, sx);
    box(d, 0, 0);
    wattron(d, COLOR_PAIR(5));
    mvwprintw(d, 0, 2, " Confirmation ");
    wattroff(d, COLOR_PAIR(5));

    mvwprintw(d, 2, 2, "%s", message.c_str());
    mvwprintw(d, 4, 2, "Press 'y' to confirm, any other key to cancel");
    wrefresh(d);

    int ch = wgetch(d);
    delwin(d);
    return (ch == 'y' || ch == 'Y');
}

// Simple informational message dialog that waits for any key
void ActivityMonitor::displayMessage(const std::string& message) {
    int h = 5;
    int w = std::min( (int)message.size() + 6, terminal_width - 4);
    int sy = std::max(0, (terminal_height - h) / 2);
    int sx = std::max(0, (terminal_width - w) / 2);
    WINDOW* d = newwin(h, w, sy, sx);
    box(d, 0, 0);
    wattron(d, COLOR_PAIR(5)); mvwprintw(d, 0, 2, " Info "); wattroff(d, COLOR_PAIR(5));
    mvwprintw(d, 2, 2, "%s", message.c_str());
    mvwprintw(d, 3, 2, "Press any key to continue");
    wrefresh(d);
    wgetch(d);
    delwin(d);
}

// ========================= MAIN LOOP =========================
void ActivityMonitor::run() {
    initializeWindows();
    collectData();

    while (running) {
        resizeWindows();
        collectData();

        displaySystemInfo();
        displayCPUInfo();
        displayMemoryInfo();
        displayDiskInfo();
        displayNetworkInfo();
        displayProcessInfo();
        displayAlert();

        int ch = getch();
        if (ch != ERR) handleInput(ch);

        std::this_thread::sleep_for(std::chrono::milliseconds(config.refresh_rate_ms));
    }

    if (sysinfo_win) delwin(toWin(sysinfo_win));
    if (cpu_win) delwin(toWin(cpu_win));
    if (mem_win) delwin(toWin(mem_win));
    if (disk_win) delwin(toWin(disk_win));
    if (network_win) delwin(toWin(network_win));
    if (process_win) delwin(toWin(process_win));
    endwin();
}
