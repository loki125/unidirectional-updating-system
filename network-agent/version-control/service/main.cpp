#include "Receiver.hpp"
#include "Store.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>

#include "utils.hpp"

void setup_dir(){
    // create the ready and processing directories to prevent race coniditions
    std::filesystem::create_directories( std::filesystem::path(set_env_var("OUTPUT_PATH")) / READY_PATH );
    std::filesystem::create_directories(std::filesystem::path(set_env_var("STORE_PATH")) / PROCESSING_DIR);
}

int main() {
    setup_dir();
    pid_t pid = fork(); // fork the process

    if (pid < 0) {
        perror("fork failed");
        return 1;
    }

    if (pid == 0) {
        // CHILD → run store loop
        Store store;
        store.run();
        _exit(0); // make sure child exits cleanly
    } else {
        // PARENT → run receiver loop

        FluteReceiver receiver;
        receiver.run();
        
        int status;
        waitpid(pid, &status, 0);
    }

    return 0;
}
