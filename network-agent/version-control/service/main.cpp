#include "Receiver.hpp"
#include "Store.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>

int main() {
    

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
