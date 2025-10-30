# Linux Activity Monitor

A real-time terminal-based system monitoring tool built with C++ and ncurses. Displays comprehensive system metrics including CPU usage per core, memory (main + swap), disk usage, network statistics, system information, and process management.


<img width="1195" height="879" alt="Screenshot 2025-10-29 115733" src="https://github.com/user-attachments/assets/7c6a1492-1e4d-4a7c-8ad9-493f9c7aa037" />

## Features

### 📊 Multi-Panel Dashboard
- **CPU Usage**: Per-core dot graph with color-coded history and real-time percentages
- **System Info**: Uptime, load average, context switches/sec, interrupts/sec
- **Disk Usage**: Mounted filesystems with used/free space
- **Memory Usage**: Dual-line graph showing Main (cyan) and Swap (yellow) memory with dynamic scaling
- **Network Usage**: Real-time RX/TX rates with horizontal bar graphs and session totals
- **Process Table**: Sortable list with PID, name, CPU%, and memory%

### 🎮 Interactive Controls
- **q** - Quit the application
- **r** - Force refresh display
- **k** - Kill selected process (with confirmation dialog)
- **c** - Sort processes by CPU usage
- **m** - Sort processes by memory usage
- **PgUp/PgDn** - Fast scroll through processes


### 🎨 Visual Features
- Color-coded metrics (green/yellow/red based on thresholds)
- Real-time graphs with dot plotting for historical data
- Responsive layout adapting to terminal size
- Load average color coding based on per-core utilization
- Separate bars for network RX (cyan) and TX (red)

### ⚙️ Advanced Features
- Optional physical CPU core aggregation (pairs logical CPUs)
- Graceful process termination (SIGTERM → SIGKILL with wait)
- Session-based network totals tracking
- Dynamic graph scaling for better visibility
- 120-sample history buffers for smooth trends

## Installation

### Prerequisites

**Linux / WSL (Ubuntu/Debian):**
```bash
sudo apt update
sudo apt install build-essential libncurses5-dev libncursesw5-dev g++
```

**Fedora/RHEL:**
```bash
sudo dnf install gcc-c++ ncurses-devel make
```

**Arch Linux:**
```bash
sudo pacman -S base-devel ncurses
```

### Build

```bash
cd activity_monitor
make clean
make -j2
```

## Usage

### Basic Usage
```bash
./activity_monitor
```

### Command-Line Options
```bash
./activity_monitor [OPTIONS]

Options:
  -r <ms>         Set refresh rate in milliseconds (default: 1000)
  -t <threshold>  Set CPU alert threshold percentage (default: 80.0)
  -a              Disable high CPU alerts
  -d              Enable debug logging to activity_monitor_debug.log
  --help          Show help message
```

### Examples
```bash
# Fast refresh every 500ms
./activity_monitor -r 500

# Set CPU alert threshold to 90%
./activity_monitor -t 90

# Run without alerts
./activity_monitor -a

# Debug mode with logging
./activity_monitor -d
```

## WSL-Specific Notes

### Running in WSL
1. Open your WSL distribution (Ubuntu, Debian, etc.)
2. Navigate to the project directory
3. Build and run inside WSL terminal (not PowerShell)

```bash
# From WSL terminal
cd /mnt/c/Users/YourName/Desktop/OS_Project-trail2/activity_monitor
make
./activity_monitor
```

### Launching from PowerShell
```powershell
wsl ./activity_monitor
```

### Known Limitations in WSL
- **Temperature sensors**: `/sys/class/thermal` not available in WSL (removed from display)
- **Some hardware metrics**: Limited compared to native Linux
- **Terminal compatibility**: Use Windows Terminal or WSL-native terminal for best rendering

## Architecture

### Project Structure
```
activity_monitor/
├── include/
│   └── monitor.h          # Data structures and class declarations
├── src/
│   ├── main.cpp           # Entry point and CLI argument parsing
│   ├── monitor.cpp        # Data collection from /proc filesystem
│   └── monitor_display.cpp # ncurses UI rendering and event loop
├── Makefile               # Build configuration
└── README.md              # This file
```

### Data Sources
- `/proc/stat` - CPU usage per core, context switches, interrupts
- `/proc/meminfo` - Memory and swap statistics
- `/proc/mounts` - Mounted filesystems
- `/proc/net/dev` - Network interface statistics
- `/proc/<pid>/stat` - Per-process CPU and memory
- `/proc/<pid>/status` - Process details
- `/proc/uptime` - System uptime
- `/proc/loadavg` - Load averages (1, 5, 15 min)

### Key Components
- **MonitorConfig**: Configuration struct for refresh rate, thresholds, options
- **CPUInfo**: Per-core and total CPU usage tracking
- **MemoryInfo**: Main memory and swap statistics
- **DiskInfo**: Per-filesystem usage information
- **Process**: Per-process metrics (PID, name, CPU%, mem%)
- **SystemInfo**: Uptime, load average, system counters
- **NetworkInfo**: RX/TX bytes and rates with history

## Technical Details

### CPU Monitoring
- Reads per-core statistics from `/proc/stat`
- Calculates delta between samples for accurate percentages
- Optional aggregation of logical cores into physical cores
- 120-sample rolling history for graph plotting
- Absolute positioning prevents graph shifting

### Memory Monitoring
- Dual-line graph with dynamic Y-axis scaling
- Tracks both main memory and swap separately
- Automatic scale adjustment to keep both lines visible
- Color-coded: cyan for main, yellow for swap

### Network Monitoring
- Tracks total RX/TX bytes since application start
- Computes per-second rates from deltas
- Horizontal bar graphs scaled to maximum observed rate
- Separate session totals and instantaneous rates

### Process Management
- Sorts by CPU or memory usage on demand
- Keyboard navigation with selection highlighting
- Kill with confirmation dialog
- Attempts SIGTERM first, then SIGKILL if needed
- Wait period configurable (default 500ms)

## Performance

- **Memory footprint**: ~5-10 MB typical
- **CPU overhead**: <1% on modern systems
- **Refresh rate**: Configurable, default 1000ms
- **History depth**: 120 samples per metric
- **Max processes displayed**: Adapts to terminal height

## Acknowledgments

Built using:
- [ncurses](https://invisible-island.net/ncurses/) for terminal UI
- Linux `/proc` filesystem for system metrics
- C++17 standard library

Inspired by traditional Unix tools like `top`, `htop`, and `glances`.

---

