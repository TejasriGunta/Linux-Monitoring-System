#include "../include/monitor.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <dirent.h>
#include <sys/statvfs.h>
#include <algorithm>
#include <iostream>
#include <sys/types.h>
#include <signal.h>
#include <thread>
#include <unistd.h>
#include <iomanip>
#include <ncurses.h>
// thread/chrono used for timed waits in killProcess
#include <thread>
#include <chrono>

ActivityMonitor::ActivityMonitor() {
    last_update = std::chrono::high_resolution_clock::now();
}

ActivityMonitor::~ActivityMonitor() {
    if (debug_file.is_open()) debug_file.close();
}

void ActivityMonitor::setConfig(const MonitorConfig& cfg) {
    config = cfg;
    // initialize first snapshot
    updateCPUInfo();
    updateMemoryInfo();
    updateDiskInfo();
    updateProcessInfo();
    updateMemoryStats();
    updateDiskLatency();

    if (config.debug_mode) debugLog("Configuration set");
}

void ActivityMonitor::collectData() {
    updateCPUInfo();
    updateMemoryInfo();
    updateDiskInfo();
    updateProcessInfo();
    updateMemoryStats();
    updateDiskLatency();
    updateNetworkInfo();
    updateTempInfo();
    updateSystemInfo();
}

// Very simple CPU reader: reads /proc/stat and computes usage since last call
void ActivityMonitor::updateCPUInfo() {
    std::ifstream f("/proc/stat");
    if (!f) throw std::runtime_error("Failed to open /proc/stat");

    std::string line;
    std::vector<unsigned long long> totals;
    std::vector<float> core_percentages;

    while (std::getline(f, line)) {
        if (line.rfind("cpu", 0) != 0) break;
        std::istringstream iss(line);
        std::string cpu_label;
        iss >> cpu_label;
        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
        user = nice = system = idle = iowait = irq = softirq = steal = 0;
        iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
        totals.push_back(total);
        // store idles as well appended (we will keep mapping)
    }

    if (totals.empty()) return;

    // If first time, just fill prev and return
    if (curr_cpu_times.empty()) {
        curr_cpu_times = totals;
        prev_cpu_times = totals;
        cpu_info.num_cores = (int)totals.size() - 1;
        cpu_info.core_usage.assign(cpu_info.num_cores, 0.0f);
        cpu_info.total_usage = 0.0f;
        return;
    }

    prev_cpu_times = curr_cpu_times;
    curr_cpu_times = totals;

    // total line is index 0
    unsigned long long prev_total = prev_cpu_times[0];
    unsigned long long curr_total = curr_cpu_times[0];
    unsigned long long total_diff = curr_total - prev_total;
    if (total_diff == 0) total_diff = 1;

    // compute idle diff: approximate using idle field positions by rereading line is complex; as approximation, compute usage from total diffs between total and sum of idle-like values
    // For simplicity, compute usage as 100 - (idle_diff/total_diff)*100 using difference of totals of all cpus compared to sum of cores
    // This is a coarse estimate but works for a demo.

    // Compute core usage approximations by reading /proc/stat again per-core with same method: derive percent as 100 * (1 - (prev/cur))? We'll compute fraction by diff/total
    int cores = (int)curr_cpu_times.size() - 1;
    cpu_info.core_usage.clear();
    for (int i = 0; i < cores; ++i) {
        unsigned long long p = prev_cpu_times[i+1];
        unsigned long long c = curr_cpu_times[i+1];
        unsigned long long diff = (c > p) ? (c - p) : 0;
        float usage = 100.0f * (float)diff / (float)( (curr_total - prev_total) == 0 ? 1 : (curr_total - prev_total) );
        if (usage > 100.0f) usage = 100.0f;
        cpu_info.core_usage.push_back(usage);
    }

    // Total approximate usage: average of core usage
    float sum = 0.0f;
    for (float v : cpu_info.core_usage) sum += v;
    cpu_info.total_usage = (cpu_info.core_usage.empty() ? 0.0f : sum / cpu_info.core_usage.size());
    cpu_info.num_cores = cores;

    // push into history buffers
    if (total_history.size() >= history_length) total_history.erase(total_history.begin());
    total_history.push_back(cpu_info.total_usage);

    // ensure cpu_history has entries per core
    if (cpu_history.size() != static_cast<size_t>(cpu_info.num_cores)) cpu_history.assign(cpu_info.num_cores, std::vector<float>());
    for (int i = 0; i < cpu_info.num_cores; ++i) {
        auto &h = cpu_history[i];
        if (h.size() >= history_length) h.erase(h.begin());
        h.push_back(cpu_info.core_usage[i]);
    }

    if (config.debug_mode) debugLog("CPU updated: total=" + std::to_string(cpu_info.total_usage));
}

void ActivityMonitor::updateMemoryInfo() {
    std::ifstream f("/proc/meminfo");
    if (!f) throw std::runtime_error("Failed to open /proc/meminfo");
    std::string line;
    unsigned long mem_total=0, mem_free=0, mem_available=0, cached=0, buffers=0, swap_total=0, swap_free=0;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string key;
        unsigned long value;
        std::string unit;
        iss >> key >> value >> unit;
        if (key=="MemTotal:") mem_total = value;
        else if (key=="MemFree:") mem_free = value;
        else if (key=="MemAvailable:") mem_available = value;
        else if (key=="Cached:") cached = value;
        else if (key=="Buffers:") buffers = value;
        else if (key=="SwapTotal:") swap_total = value;
        else if (key=="SwapFree:") swap_free = value;
    }

    memory_info.total = mem_total;
    memory_info.free = mem_free;
    memory_info.available = mem_available;
    memory_info.used = (mem_total > mem_available) ? (mem_total - mem_available) : 0;
    memory_info.percent_used = (mem_total==0) ? 0.0f : (100.0f * memory_info.used / mem_total);
    memory_info.cached = cached;
    memory_info.buffers = buffers;
    memory_info.swap_total = swap_total;
    memory_info.swap_free = swap_free;
    memory_info.swap_used = (swap_total>swap_free)?(swap_total - swap_free):0;
    memory_info.swap_percent_used = (swap_total==0)?0.0f:(100.0f * memory_info.swap_used / swap_total);

    if (config.debug_mode) debugLog("Memory updated: " + std::to_string(memory_info.percent_used) + "%");

    if (mem_history.size() >= history_length) mem_history.erase(mem_history.begin());
    mem_history.push_back(memory_info.percent_used);
    if (swap_history.size() >= history_length) swap_history.erase(swap_history.begin());
    swap_history.push_back(memory_info.swap_percent_used);
}

void ActivityMonitor::updateDiskInfo() {
    std::ifstream mounts("/proc/mounts");
    if (!mounts) throw std::runtime_error("Failed to open /proc/mounts");
    disk_info.clear();
    std::string line;
    while (std::getline(mounts, line)) {
        std::istringstream iss(line);
        std::string device, mount_point, fs_type, opts;
        int dump, pass;
        iss >> device >> mount_point >> fs_type >> opts >> dump >> pass;
        // skip pseudo filesystems
        if (fs_type=="proc" || fs_type=="sysfs" || fs_type=="tmpfs" || fs_type=="devtmpfs" ) continue;

        struct statvfs st;
        if (statvfs(mount_point.c_str(), &st) != 0) continue;

        DiskInfo d;
        d.device = device;
        d.mount_point = mount_point;
        unsigned long block_size = st.f_frsize;
        d.total_space = (st.f_blocks * block_size) / 1024;
        d.free_space = (st.f_bfree * block_size) / 1024;
        d.used_space = (d.total_space > d.free_space) ? (d.total_space - d.free_space) : 0;
        d.percent_used = (d.total_space==0)?0.0f:(100.0f * d.used_space / d.total_space);
        disk_info.push_back(d);
    }

    if (config.debug_mode) debugLog("Disk info updated: " + std::to_string(disk_info.size()) + " mounts");
}

void ActivityMonitor::updateProcessInfo() {
    processes.clear();
    DIR* pd = opendir("/proc");
    if (!pd) throw std::runtime_error("Failed to open /proc");
    struct dirent* ent;

    // compute total diff for CPU jiffies
    unsigned long long total_diff = 0;
    if (curr_cpu_times.size() > 0 && prev_cpu_times.size() > 0) {
        total_diff = (curr_cpu_times[0] > prev_cpu_times[0]) ? (curr_cpu_times[0] - prev_cpu_times[0]) : 0;
    }
    if (total_diff == 0) total_diff = 1;

    std::unordered_map<int, unsigned long long> seen_pids;

    while ((ent = readdir(pd)) != nullptr) {
        if (ent->d_type != DT_DIR) continue;
        std::string name = ent->d_name;
        if (!std::all_of(name.begin(), name.end(), ::isdigit)) continue;
        int pid = std::stoi(name);

        // read /proc/<pid>/stat
        std::string statpath = "/proc/" + name + "/stat";
        std::ifstream s(statpath);
        if (!s) continue;
        std::string statline;
        std::getline(s, statline);
        // parse: pid (comm) rest...
        size_t pos = statline.find(')');
        if (pos == std::string::npos) continue;
        std::string comm = statline.substr(0, pos+1);
        std::string rest = (pos+2 < statline.size()) ? statline.substr(pos+2) : std::string();
        std::istringstream iss(rest);
        // tokens: rest starts at field 3, so utime is tokens[11], stime tokens[12]
        std::vector<std::string> toks;
        std::string tok;
        while (iss >> tok) toks.push_back(tok);
        unsigned long long utime = 0, stime = 0;
        if (toks.size() > 12) {
            try { utime = std::stoull(toks[11]); } catch(...) { utime = 0; }
            try { stime = std::stoull(toks[12]); } catch(...) { stime = 0; }
        }
        unsigned long long total_time = utime + stime;

        // read VmRSS for memory
        std::string statuspath = "/proc/" + name + "/status";
        std::ifstream st(statuspath);
        std::string line;
        unsigned long vmrss = 0;
        std::string proc_name = comm;
        while (std::getline(st, line)) {
            if (line.rfind("Name:",0)==0) {
                std::istringstream is2(line);
                std::string k, v; is2 >> k >> v; proc_name = v; }
            if (line.rfind("VmRSS:",0)==0) { std::istringstream is2(line); std::string k; unsigned long v; is2 >> k >> v; vmrss = v; }
        }

        Process p;
        p.pid = pid;
        // sanitize name
        if (!proc_name.empty() && proc_name.front()=='(' && proc_name.back()==')') proc_name = proc_name.substr(1, proc_name.size()-2);
        p.name = proc_name;

        // compute cpu percent using previous proc times
        unsigned long long prev_pt = 0;
        auto it = prev_proc_times.find(pid);
        if (it != prev_proc_times.end()) prev_pt = it->second;
        unsigned long long delta_proc = (total_time > prev_pt) ? (total_time - prev_pt) : 0;
        int ncores = std::max(1, cpu_info.num_cores);
        float cpu_pct = 0.0f;
        if (total_diff > 0) cpu_pct = 100.0f * (float)delta_proc * (float)ncores / (float)total_diff;
        p.cpu_percent = cpu_pct;
        p.mem_percent = (memory_info.total==0)?0.0f:(100.0f * (float)vmrss / (float)memory_info.total);

        // store current proc time for next interval
        prev_proc_times[pid] = total_time;
        seen_pids[pid] = total_time;

        processes.push_back(p);
    }
    closedir(pd);

    // remove stale entries from prev_proc_times
    for (auto it = prev_proc_times.begin(); it != prev_proc_times.end(); ) {
        if (seen_pids.find(it->first) == seen_pids.end()) it = prev_proc_times.erase(it); else ++it;
    }

    // sort according to current sort type
    sortProcesses();
}

void ActivityMonitor::updateMemoryStats() {
    if (memory_info.total == 0) { memory_info.cache_hit_rate = -1.0f; memory_info.latency_ns = -1.0f; return; }
    float cache_percentage = 100.0f * (float)(memory_info.cached + memory_info.buffers) / (float)memory_info.total;
    memory_info.cache_hit_rate = 70.0f + cache_percentage * 0.25f;
    if (memory_info.cache_hit_rate > 99.0f) memory_info.cache_hit_rate = 99.0f;
    memory_info.latency_ns = 60.0f + (40.0f * memory_info.percent_used / 100.0f);
}

void ActivityMonitor::updateDiskLatency() {
    // For demo, simulate latency based on usage
    for (auto &d : disk_info) {
        d.read_latency_ms = 1.0f + (d.percent_used / 100.0f) * 50.0f; // 1ms to ~51ms
    }
}

// Read network totals from /proc/net/dev and compute KB/s rates
void ActivityMonitor::updateNetworkInfo() {
    std::ifstream f("/proc/net/dev");
    if (!f) return;
    std::string line;
    // skip headers
    std::getline(f, line);
    std::getline(f, line);

    unsigned long long total_rx = 0, total_tx = 0;
    while (std::getline(f, line)) {
        std::istringstream iss(line);
        std::string iface;
        unsigned long long rx_bytes, tx_bytes;
        if (!(iss >> iface)) continue;
        if (!iface.empty() && iface.back() == ':') iface.pop_back();
        iss >> rx_bytes;
        // skip 7 fields to tx
        for (int i = 0; i < 7; ++i) { unsigned long long tmp; iss >> tmp; }
        iss >> tx_bytes;
        total_rx += rx_bytes;
        total_tx += tx_bytes;
    }

    // calculate rates using last_update
    auto now = std::chrono::high_resolution_clock::now();
    static auto prev_time = last_update;
    double seconds = std::chrono::duration<double>(now - prev_time).count();
    if (seconds <= 0) seconds = 1.0;

    float rx_kbps = 0.0f, tx_kbps = 0.0f;
    if (prev_net_rx != 0 && prev_net_tx != 0) {
        rx_kbps = static_cast<float>((total_rx - prev_net_rx) / 1024.0 / seconds);
        tx_kbps = static_cast<float>((total_tx - prev_net_tx) / 1024.0 / seconds);
    }

    prev_net_rx = total_rx;
    prev_net_tx = total_tx;
    prev_time = now;

    // store current absolute totals and session start if first
    curr_net_rx_bytes = total_rx;
    curr_net_tx_bytes = total_tx;
    if (net_start_rx == 0) net_start_rx = total_rx;
    if (net_start_tx == 0) net_start_tx = total_tx;

    if (net_rx_history.size() >= history_length) net_rx_history.erase(net_rx_history.begin());
    if (net_tx_history.size() >= history_length) net_tx_history.erase(net_tx_history.begin());
    net_rx_history.push_back(rx_kbps);
    net_tx_history.push_back(tx_kbps);
}

// Read thermal sensors if available (/sys/class/thermal)
void ActivityMonitor::updateTempInfo() {
    temperatures.clear();
    // try thermal_zone entries
    for (int i = 0; i < 8; ++i) {
        std::string base = "/sys/class/thermal/thermal_zone" + std::to_string(i) + "/";
        std::ifstream typef(base + "type");
        std::ifstream tempf(base + "temp");
        if (!typef.good() || !tempf.good()) continue;
        std::string type; std::getline(typef, type);
        long tempm = 0; tempf >> tempm;
        float deg = tempm / 1000.0f;
        temperatures.emplace_back(type, deg);
    }
}

void ActivityMonitor::updateSystemInfo() {
    // Read uptime
    std::ifstream uptime_file("/proc/uptime");
    if (uptime_file) {
        uptime_file >> system_info.uptime_seconds;
        uptime_file.close();
    }

    // Read load average
    std::ifstream loadavg_file("/proc/loadavg");
    if (loadavg_file) {
        loadavg_file >> system_info.load_1min >> system_info.load_5min >> system_info.load_15min;
        loadavg_file.close();
    }

    // Read context switches and interrupts from /proc/stat
    std::ifstream stat_file("/proc/stat");
    if (stat_file) {
        std::string line;
        while (std::getline(stat_file, line)) {
            if (line.rfind("ctxt ", 0) == 0) {
                std::istringstream iss(line);
                std::string label;
                iss >> label >> system_info.total_ctx_switches;
            } else if (line.rfind("intr ", 0) == 0) {
                std::istringstream iss(line);
                std::string label;
                unsigned long long first_val;
                iss >> label >> first_val;
                system_info.total_interrupts = first_val;
            }
        }
        stat_file.close();
    }

    // Calculate rates (per second)
    static auto last_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - last_time).count();
    
    if (elapsed > 0 && system_info.prev_ctx_switches > 0) {
        system_info.ctx_switches_per_sec = 
            (system_info.total_ctx_switches - system_info.prev_ctx_switches) / elapsed;
        system_info.interrupts_per_sec = 
            (system_info.total_interrupts - system_info.prev_interrupts) / elapsed;
    }

    system_info.prev_ctx_switches = system_info.total_ctx_switches;
    system_info.prev_interrupts = system_info.total_interrupts;
    last_time = now;
}

std::string ActivityMonitor::formatSize(unsigned long size_kb) {
    std::ostringstream oss;
    if (size_kb < 1024) oss << size_kb << " KB";
    else if (size_kb < 1024*1024) { oss << (size_kb/1024.0) << " MB"; }
    else { oss << (size_kb/(1024.0*1024.0)) << " GB"; }
    return oss.str();
}

std::string ActivityMonitor::formatLatency(float latency, bool is_memory) {
    std::ostringstream oss; oss << std::fixed << std::setprecision(2);
    if (latency < 0) return "N/A";
    if (is_memory) oss << latency << " ns"; else oss << latency << " ms";
    return oss.str();
}

std::string ActivityMonitor::createBar(float percent, int width, bool) {
    if (width < 10) width = 10;
    int barw = width - 7; // space for percent
    int fill = static_cast<int>(barw * percent / 100.0f + 0.5);
    if (fill > barw) fill = barw;
    std::string s = "[";
    for (int i=0;i<barw;i++) s += (i<fill)?'#':' ';
    s += "] ";
    std::ostringstream oss; oss << std::fixed << std::setprecision(1) << percent << "%";
    s += oss.str();
    return s;
}

void ActivityMonitor::debugLog(const std::string& msg) {
    if (!config.debug_mode) return;
    if (!debug_file.is_open()) debug_file.open("activity_monitor_debug.log", std::ios::out | std::ios::app);
    debug_file << msg << std::endl;
    std::cerr << "DEBUG: " << msg << std::endl;
}

// Kill process - best-effort
bool ActivityMonitor::killProcess(int pid) {
    if (pid <= 0) return false;
    // send polite termination first
    int r = ::kill(pid, SIGTERM);
    bool success = false;
    if (r == 0) {
        // wait up to config.kill_wait_ms for the process to exit
        int waited = 0;
        const int interval = 50; // ms
        while (waited < config.kill_wait_ms) {
            // kill(pid, 0) checks for existence: returns -1 and sets errno==ESRCH if not present
            if (::kill(pid, 0) == -1) {
                // process no longer exists
                success = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            waited += interval;
        }
        if (!success) {
            // attempt forceful kill
            ::kill(pid, SIGKILL);
            // small pause to let kernel reap
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (::kill(pid, 0) == -1) success = true;
        }
    } else {
        // SIGTERM syscall failed — try SIGKILL immediately
        ::kill(pid, SIGKILL);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (::kill(pid, 0) == -1) success = true;
    }

    // Refresh data and provide feedback to the user
    collectData();
    if (success) displayMessage("Process " + std::to_string(pid) + " terminated successfully.");
    else displayMessage("Failed to terminate process " + std::to_string(pid) + ". Check permissions.");
    return success;
}

void ActivityMonitor::killHighestCPUProcess() {
    if (processes.empty()) return;
    int pid = processes[0].pid;
    killProcess(pid);
}

void ActivityMonitor::handleInput(int ch) {
    switch (ch) {
        case 'q': running = false; break;
        case 'r': collectData(); break;
        case 'k': {
            // kill selected process if any
            if (!processes.empty() && process_selected >= 0 && process_selected < (int)processes.size()) {
                int pid = processes[process_selected].pid;
                std::ostringstream oss; oss << "Kill process " << pid << " (" << processes[process_selected].name << ")?";
                if (displayConfirmationDialog(oss.str())) killProcess(pid);
            }
            break;
        }
        case 'c': process_sort_type = 0; sortProcesses(); break;
        case 'm': process_sort_type = 1; sortProcesses(); break;
        case KEY_UP:
            if (process_selected > 0) {
                process_selected--;
                if (process_selected < process_list_offset) process_list_offset = process_selected;
            }
            break;
        case KEY_DOWN:
            if (process_selected < (int)processes.size() - 1) {
                process_selected++;
                int rows = terminal_height / 2 - 3;
                if (process_selected >= process_list_offset + rows) process_list_offset = process_selected - rows + 1;
            }
            break;
        case KEY_PPAGE:
            process_list_offset = std::max(0, process_list_offset - 10);
            process_selected = std::max(0, process_selected - 10);
            break;
        case KEY_NPAGE:
            process_list_offset = std::min(std::max(0, (int)processes.size()-1), process_list_offset + 10);
            process_selected = std::min((int)processes.size()-1, process_selected + 10);
            break;
        case KEY_HOME:
            process_list_offset = 0; process_selected = 0; break;
        case KEY_END:
            process_list_offset = std::max(0, (int)processes.size() - 1); process_selected = std::max(0, (int)processes.size() - 1); break;
        default: break;
    }
}


void ActivityMonitor::runDebugMode() {
    collectData();
    debugLog("=== Debug-only mode output ===");
    debugLog("CPU: " + std::to_string(cpu_info.total_usage));
    debugLog("Memory: " + std::to_string(memory_info.percent_used));
    for (auto &d : disk_info) debugLog("Disk: " + d.mount_point + " " + formatSize(d.total_space));
}

void ActivityMonitor::sortProcesses() {
    if (process_sort_type == 0) {
        std::sort(processes.begin(), processes.end(), [](const Process& a, const Process& b){
            if (a.cpu_percent == b.cpu_percent) return a.mem_percent > b.mem_percent;
            return a.cpu_percent > b.cpu_percent;
        });
    } else {
        std::sort(processes.begin(), processes.end(), [](const Process& a, const Process& b){
            if (a.mem_percent == b.mem_percent) return a.cpu_percent > b.cpu_percent;
            return a.mem_percent > b.mem_percent;
        });
    }
    // clamp selection
    if (process_selected >= (int)processes.size()) process_selected = std::max(0, (int)processes.size() - 1);
}
