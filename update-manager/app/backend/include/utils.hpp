#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <mutex>
#include <array>
#include <tuple>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>

// Third-party headers
#include <Transmitter.h>
#include <nlohmann/json.hpp>

#define SET_PATH(func) "/" #func
#define CONNECTION_TIMEOUT 10 // seconds
#define READ_TIMEOUT 60 // seconds

using json = nlohmann::json;
namespace fs = std::filesystem;
extern char **environ; // Required for posix_spawn so Linker wont get angry

/*
    PACKGET SERVICE UTILS
    ---------------------
    Common utilities, data structures, and helper functions for package services.
    This includes:
    - Command execution helpers (with separate stdout/stderr capture)
    - Data structures for package metadata and update manifests
    - JSON serialization/deserialization for these structures
    - Constants for package management (e.g., ignored packages, essential packages) 
*/

const std::set<std::string> IGNORE_PACKAGES = {"dpkg", "awk"};
const std::set<std::string> ESSENTIAL_PACKAGES = {"base-files", "bash", "coreutils", "debianutils", "libc-bin", "util-linux"};

struct CommandResult {
    int exit_code;
    std::string stdout_res;
    std::string stderr_res;
};

/**
 * Executes a shell command, captures stdout and stderr separately,
 * and returns the exit status.
 */

 inline std::string encode_url(const std::string &value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (auto i = value.begin(), n = value.end(); i != n; ++i) {
        std::string::value_type c = (*i);

        // Keep alphanumeric and other safe characters
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
            continue;
        }

        // Any other characters are percent-encoded
        escaped << std::uppercase << '%' << std::setw(2) << int((unsigned char)c);
    }

    return escaped.str();
}

inline int execute_status_cmd(const std::string& cmd) {
    // std::system returns the shell exit status directly
    int status = std::system(cmd.c_str());
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

inline CommandResult execute_command(const std::string& cmd) {
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) == -1 || pipe(err_pipe) == -1) {
        return {-1, "", "Failed to create pipes"};
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    // Setup redirection in the child
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);

    // Close the write-ends in the child so it doesn't leak them
    posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[1]);

    const char* argv[] = {"/bin/sh", "-c", cmd.c_str(), nullptr};
    pid_t pid;
    
    // Spawn the process
    int status = posix_spawn(&pid, "/bin/sh", &actions, nullptr, (char* const*)argv, environ);
    
    // Cleanup file actions immediately after spawn
    posix_spawn_file_actions_destroy(&actions);

    if (status != 0) {
        return {-1, "", "posix_spawn failed"};
    }

    // Parent process: Close the write-ends 
    close(out_pipe[1]);
    close(err_pipe[1]);

    auto read_from = [](int fd) {
        std::string output;
        char buffer[4096];
        ssize_t bytes_read;
        while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
            output.append(buffer, bytes_read);
        }
        return output;
    };

    std::string out = read_from(out_pipe[0]);
    std::string err = read_from(err_pipe[0]);

    close(out_pipe[0]);
    close(err_pipe[0]);

    int wait_status;
    waitpid(pid, &wait_status, 0);

    return {WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : -1, out, err};
}

struct PackageMetadata {
    std::string Package;
    std::string Version;
    std::string Type;
    std::string Architecture;
    std::string Store_Path;
    std::vector<std::vector<std::string>> Dependencies;
    std::string SHA256;
    long long Installed_Size = 0;
    long long Size = 0;
    std::string Filename;
    bool Latest = false;
    std::string Timestamp = "";

    std::string generate_id() const {
        return Package + "_" + Version + "_" + Architecture;
    }

    static std::string build_id(const std::string& package, const std::string& version, const std::string& arch) {
        return package + "_" + version + "_" + arch;
    }

    void compute_store_path() {
        Store_Path = SHA256 + "-" + Package + "-" + Version;
    }

    json to_json() const {
        return json{
            {"Package", this->Package},
            {"Version", this->Version},
            {"Type", this->Type},
            {"Architecture", this->Architecture},
            {"Store_Path", this->Store_Path},
            {"Dependencies", this->Dependencies},
            {"SHA256", this->SHA256},
            {"Installed-Size", this->Installed_Size},
            {"Size", this->Size},
            {"Filename", this->Filename},
            {"Latest", this->Latest},
            {"Timestamp", this->Timestamp}
        };
    }
};

struct UpdateManifest {
    std::string update_id;
    std::string pkgs_type;
    std::string format_version;
    std::string timestamp;
    long long total_size_byte;
    std::vector<PackageMetadata> packages;

    json to_json() const {
        json pkg_array = nlohmann::json::array();
        for (const auto& pkg : packages) {
            // Push the JSON OBJECT, not the string dump
            pkg_array.push_back(pkg.to_json_obj()); 
        }

        return json{
            {"Update_id", this->update_id},
            {"Type", this->pkgs_type},
            {"Format_version", this->format_version},
            {"Timestamp", this->timestamp},
            {"Total_size_byte", this->total_size_byte},
            {"Packages", pkg_array}
        };
    }
};


/*
    BROADCASTER SERVICE UTILS
    -------------------------
    Common utilities, data structures, and helper functions for the broadcaster service.
    This includes:
    - Data structures for managing broadcast targets and file entries
    - Helper functions for JSON parsing and response formatting
*/

struct ft_arguments {
  std::string mcast_target = {};
  bool enable_ipsec = false;
  bool use_gzip = false;
  bool gen_etags = false;
  const char *aes_key = {};
  unsigned short mcast_port = 40085;
  unsigned short mtu = 1500;
  uint32_t rate_limit = 1000;
  uint64_t tsi = 16;
  size_t retransmit_count = 1;
};

struct fileEntry {
    std::shared_ptr<LibFlute::Transmitter::FileDescription> file;
    size_t transmitted_count;
    
    // ADD THESE TWO LINES to keep the strings alive in memory
    std::string kept_path; 
    std::string kept_name;

    // Update constructor to store them
    fileEntry(LibFlute::Transmitter::FileDescription* f, std::string p, std::string n)
        : file(f), transmitted_count(0), kept_path(std::move(p)), kept_name(std::move(n)) {}
};

struct Target {
    ft_arguments args;
    std::unique_ptr<LibFlute::Transmitter> transmitter;
    std::list<fileEntry> files;

    std::atomic<size_t> pending_files{0};
};

inline const char* get_env_var(const std::string& name){
    const char* var = std::getenv(name.data());
    if (!var) {
        throw std::runtime_error("Environment variable " + name + " is not set.");
    }
    return var;
}