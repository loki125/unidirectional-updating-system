#include <iostream>
#include <filesystem>
#include <sys/wait.h>
#include <unistd.h>

// Third-party
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

// Project headers
#include "Receiver.hpp"
#include "Store.hpp"
#include "Reporter.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;

/**
 * Global logging setup
 */
void setup_logging() {
    auto console = spdlog::stdout_color_mt("console");
    spdlog::set_default_logger(console);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
    spdlog::set_level(spdlog::level::debug);
}

/**
 * Environment and Directory Setup
 */
void initialize_environment() {
    try {
        fs::path output_path = fs::path(set_env_var("OUTPUT_PATH")) / READY_PATH;
        fs::path store_path  = fs::path(set_env_var("STORE_PATH")) / PROCESSING_DIR;

        if (fs::create_directories(output_path)) spdlog::info("Created directory: {}", output_path.string());
        if (fs::create_directories(store_path))  spdlog::info("Created directory: {}", store_path.string());
        
    } catch (const fs::filesystem_error& e) {
        spdlog::critical("Failed to create system directories: {}", e.what());
        std::exit(EXIT_FAILURE);
    }
}

/**
 * Worker Wrapper: Forks a process and runs the provided task.
 */
pid_t spawn_worker(const std::string& name, std::function<void()> task) {
    pid_t pid = fork();

    if (pid < 0) {
        spdlog::error("Failed to fork worker '{}': {}", name, strerror(errno));
        return -1;
    }

    if (pid == 0) {
        // --- CHILD PROCESS ---
        spdlog::info("Worker '{}' started (PID: {})", name, getpid());
        try {
            task();
        } catch (const std::exception& e) {
            spdlog::error("Worker '{}' crashed with exception: {}", name, e.what());
            _exit(EXIT_FAILURE);
        }
        spdlog::info("Worker '{}' finished successfully.", name);
        _exit(EXIT_SUCCESS);
    }

    // PARENT PROCESS 
    return pid;
}

/**
 * Networking Logic: Spawns Reporter and runs Receiver.
 */
void run_networking_stack() {
    pid_t reporter_pid = spawn_worker("Reporter", []() {
        Reporter reporter;
        //reporter.run();
    });

    spdlog::info("Starting Receiver in Networking Stack...");
    FluteReceiver receiver;
    receiver.run();

    // Cleanup reporter if receiver ever exits
    if (reporter_pid > 0) {
        int status;
        waitpid(reporter_pid, &status, 0);
    }
}

int main() {
    setup_logging();
    spdlog::info("System initializing...");
    
    initialize_environment();

    pid_t store_pid = spawn_worker("Store", []() {
        Store store;
        store.run();
    });

    // Run Networking stackter
    run_networking_stack();

    // Wait for the Store process to finish
    if (store_pid > 0) {
        int status;
        waitpid(store_pid, &status, 0);
        spdlog::info("Store worker exited.");
    }

    spdlog::info("Main process exiting.");
    return EXIT_SUCCESS;
}