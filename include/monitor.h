#pragma once
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>
#include <fstream>

struct MonitorConfig {
    int refresh_rate_ms = 1000;
    float cpu_threshold = 80.0f;
    bool show_alert = true;
    bool system_notifications = false;
    bool debug_mode = false;
    bool debug_only_mode = false;
    // How long (ms) to wait after sending SIGTERM before attempting SIGKILL
    int kill_wait_ms = 500;
    // Size of plotted CPU dot in characters (1 = single cell, 2 = double-wide)
    int dot_size = 2;
    // If true, aggregate logical CPUs into physical cores (pairs) for display
    bool aggregate_physical = true;
};

struct CPUInfo {
    float total_usage = 0.0f;
    std::vector<float> core_usage;
    int num_cores = 0;
};

struct MemoryInfo {
    unsigned long total = 0;
    unsigned long free = 0;
    unsigned long available = 0;
    unsigned long used = 0;
    float percent_used = 0.0f;

    unsigned long swap_total = 0;
    unsigned long swap_free = 0;
    unsigned long swap_used = 0;
    float swap_percent_used = 0.0f;

    unsigned long cached = 0;
    unsigned long buffers = 0;

    float cache_hit_rate = -1.0f;
    float latency_ns = -1.0f; // simulated
};

struct DiskInfo {
    std::string device;
    std::string mount_point;
    unsigned long total_space = 0; // KB
    unsigned long free_space = 0;  // KB
    unsigned long used_space = 0;  // KB
    float percent_used = 0.0f;
    float read_latency_ms = -1.0f; // simulated
};

struct Process {
    int pid = 0;
    std::string name;
    float cpu_percent = 0.0f;
    float mem_percent = 0.0f;
};

struct SystemInfo {
    double uptime_seconds = 0.0;
    float load_1min = 0.0f;
    float load_5min = 0.0f;
    float load_15min = 0.0f;
    unsigned long long total_ctx_switches = 0;
    unsigned long long total_interrupts = 0;
    unsigned long long prev_ctx_switches = 0;
    unsigned long long prev_interrupts = 0;
    float ctx_switches_per_sec = 0.0f;
    float interrupts_per_sec = 0.0f;
};

struct DiskIOInfo {
    float read_mb_per_sec = 0.0f;
    float write_mb_per_sec = 0.0f;
    float read_ops_per_sec = 0.0f;
    float write_ops_per_sec = 0.0f;
    float io_busy_percent = 0.0f;
    unsigned long long prev_reads = 0;
    unsigned long long prev_writes = 0;
    unsigned long long prev_read_sectors = 0;
    unsigned long long prev_write_sectors = 0;
    unsigned long long prev_io_ticks = 0;
};

class ActivityMonitor {
public:
    ActivityMonitor();
    ~ActivityMonitor();

    // Configuration
    void setConfig(const MonitorConfig& cfg);

    // Run loop
    void run();
    void runDebugMode();

    // Data collection
    void collectData();
    void updateCPUInfo();
    void updateMemoryInfo();
    void updateDiskInfo();
    void updateProcessInfo();
    void updateMemoryStats();
    void updateDiskLatency();
    void updateTempInfo();
    void updateSystemInfo();
    void updateDiskIOInfo();

    // Helpers
    std::string formatSize(unsigned long size_kb);
    std::string formatLatency(float latency, bool is_memory);
    std::string createBar(float percent, int width, bool use_color = false);

    // UI
    void initializeWindows();
    void resizeWindows();
    void displayCPUInfo();
    void displayMemoryInfo();
    void displayDiskInfo();
    void displaySystemInfo();
    void displayDiskIOInfo();
    void displayProcessInfo();
    void displayAlert();
    bool displayConfirmationDialog(const std::string& message);
    // Show an informational message dialog (waits for any key)
    void displayMessage(const std::string& message);

    // Actions
    bool killProcess(int pid);
    void killHighestCPUProcess();

    // Input
    void handleInput(int ch);

    // debug
    void debugLog(const std::string& msg);

private:
    MonitorConfig config;
    CPUInfo cpu_info;
    MemoryInfo memory_info;
    SystemInfo system_info;
    DiskIOInfo diskio_info;
    std::vector<DiskInfo> disk_info;
    std::vector<Process> processes;

    // History buffers for sparklines
    size_t history_length = 120;
    std::vector<std::vector<float>> cpu_history; // per-core history (percent)
    std::vector<float> total_history;
    std::vector<float> mem_history;
    std::vector<float> swap_history;

    // Disk I/O history
    std::vector<float> diskio_read_history;  // MB/s
    std::vector<float> diskio_write_history; // MB/s

    // Temperatures (label, degC)
    std::vector<std::pair<std::string, float>> temperatures;

    // ncurses windows (forward declare as void* to avoid including ncurses here)
    void* sysinfo_win = nullptr;
    void* cpu_win = nullptr;
    void* mem_win = nullptr;
    void* disk_win = nullptr;
    void* diskio_win = nullptr;
    void* process_win = nullptr;
    void* alert_win = nullptr;

    bool running = true;
    int process_sort_type = 0; // 0 = CPU, 1 = memory
    int process_list_offset = 0;
    int process_selected = 0; // index in processes vector
    
    // Search functionality
    bool search_mode = false;
    std::string search_query = "";
    std::vector<Process> filtered_processes;

    int terminal_height = 24;
    int terminal_width = 80;

    // for CPU delta calculations
    std::vector<unsigned long long> prev_cpu_times;
    std::vector<unsigned long long> curr_cpu_times;
    std::unordered_map<int, unsigned long long> prev_proc_times;

    // sort helper
    void sortProcesses();

    std::chrono::high_resolution_clock::time_point last_update;
    std::chrono::high_resolution_clock::time_point last_notification;

    // debug file
    std::ofstream debug_file;
    // Terminal capabilities
    bool use_256_colors = false;
};
