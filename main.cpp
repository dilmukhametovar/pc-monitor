#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

const std::string SERIAL_PORT = "/dev/rfcomm0";
const int BAUD_RATE = B115200;
const int INTERVAL_SEC = 2;

struct CoreTimes {
    unsigned long long user = 0, nice = 0, system = 0, idle = 0,
                       iowait = 0, irq = 0, softirq = 0, steal = 0;
    unsigned long long total() const {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }
    unsigned long long idle_total() const {
        return idle + iowait;
    }
};

std::vector<CoreTimes> read_proc_stat() {
    std::vector<CoreTimes> cores;
    std::ifstream file("/proc/stat");
    std::string line;

    while (std::getline(file, line)) {
        if (line.rfind("cpu", 0) != 0 || line.rfind("cpu ", 0) == 0) continue;

        std::istringstream iss(line);
        std::string cpu_label;
        CoreTimes ct;
        iss >> cpu_label >> ct.user >> ct.nice >> ct.system >> ct.idle
            >> ct.iowait >> ct.irq >> ct.softirq >> ct.steal;
        cores.push_back(ct);
    }
    return cores;
}

std::vector<double> get_core_usages() {
    auto prev = read_proc_stat();
    if (prev.empty()) return {};

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto curr = read_proc_stat();
    if (curr.size() != prev.size()) return {};

    std::vector<double> usages;
    usages.reserve(curr.size());

    for (size_t i = 0; i < curr.size(); ++i) {
        unsigned long long total_diff = curr[i].total() - prev[i].total();
        unsigned long long idle_diff = curr[i].idle_total() - prev[i].idle_total();

        double usage = 0.0;
        if (total_diff > 0) {
            usage = (1.0 - static_cast<double>(idle_diff) / total_diff) * 100.0;
        }
        usages.push_back(usage);
    }
    return usages;
}

long read_long_from_file(const std::string& path) {
    std::ifstream f(path);
    long value = 0;
    if (f >> value) return value;
    return -1;
}

std::string read_string_from_file(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

std::vector<double> get_core_temps() {
    const char* hwmon_root = "/sys/class/hwmon";
    DIR* dir = opendir(hwmon_root);
    if (!dir) return {};

    std::string target_dir;
    struct dirent* entry;

    // looking for "hwmon*" directory with "coretemp" or "k10temp"
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        std::string hwmon_path = std::string(hwmon_root) + "/" + entry->d_name;
        std::string name_path = hwmon_path + "/name";
        std::string chip_name = read_string_from_file(name_path);

        if (chip_name == "coretemp" || chip_name == "k10temp") {
            target_dir = hwmon_path;
            break;
        }
    }
    closedir(dir);

    if (target_dir.empty()) return {};

    std::vector<std::pair<int, double>> temps_with_index;

    DIR* hw_dir = opendir(target_dir.c_str());
    if (!hw_dir) return {};

    while ((entry = readdir(hw_dir)) != nullptr) {
        std::string fname = entry->d_name;
        if (fname.rfind("temp", 0) != 0) continue;
        if (fname.size() < 11 || fname.substr(fname.size() - 6) != "_input") continue;

        std::string num_str = fname.substr(4, fname.size() - 10);
        int idx = std::stoi(num_str);

        std::string full_path = target_dir + "/" + fname;
        long milli_c = read_long_from_file(full_path);
        if (milli_c >= 0) {
            temps_with_index.emplace_back(idx, milli_c / 1000.0);
        }
    }
    closedir(hw_dir);

    std::sort(temps_with_index.begin(), temps_with_index.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<double> temps;
    temps.reserve(temps_with_index.size());
    for (const auto& p : temps_with_index) {
        temps.push_back(p.second);
    }
    return temps;
}

int open_serial_port(const std::string& port) {
    int fd = ::open(port.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        std::cerr << "Can't open " << port << ": " << strerror(errno) << std::endl;
        return -1;
    }

    struct termios tty {};
    if (tcgetattr(fd, &tty) != 0) {
        std::cerr << "tcgetattr failed" << std::endl;
        close(fd);
        return -1;
    }

    cfsetospeed(&tty, BAUD_RATE);
    cfsetispeed(&tty, BAUD_RATE);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        std::cerr << "tcsetattr failed" << std::endl;
        close(fd);
        return -1;
    }
    return fd;
}

int main() {
    int fd = open_serial_port(SERIAL_PORT);
    if (fd < 0) {
        std::cerr << "Can't open serial port" << std::endl;
        return 1;
    }
    std::cout << "Port " << SERIAL_PORT << " is opened. Send data every "
              << INTERVAL_SEC << " s" << std::endl;

    while (true) {
        auto usages = get_core_usages();
        auto temps = get_core_temps();

        // Format: U0,U1,...;T0,T1,...
        std::ostringstream oss;
        for (size_t i = 0; i < usages.size(); ++i) {
            if (i > 0) oss << ",";
            oss << std::fixed << std::setprecision(1) << usages[i];
        }
        oss << ";";
        for (size_t i = 0; i < temps.size(); ++i) {
            if (i > 0) oss << ",";
            oss << std::fixed << std::setprecision(1) << temps[i];
        }
        oss << "\n";

        std::string line = oss.str();
        ssize_t written = ::write(fd, line.c_str(), line.size());
        if (written < 0) {
            std::cerr << "Write error: " << strerror(errno) << std::endl;
            break;
        }
        std::cout << "Sent: " << line;
        std::this_thread::sleep_for(std::chrono::seconds(INTERVAL_SEC));
    }

    close(fd);
    return 0;
}
