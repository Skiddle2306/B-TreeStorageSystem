#include "page_manager.h"
#include <iostream>
#include <cstring>

int main() {
    const std::string filename = "pm_test.db";
    // Ensure clean start
    std::remove(filename.c_str());
    PageManager pm(filename, 4); // 4 frames

    // Allocate a new page
    page_id_t pid;
    char* data = pm.newPage(pid, LatchMode::EXCLUSIVE);
    // Write some data
    const char* msg = "HelloPage";
    std::memcpy(data, msg, std::strlen(msg) + 1);
    pm.unpin(pid, true, LatchMode::EXCLUSIVE);

    // Flush to ensure persisted
    pm.flushAll();

    // Read back the page
    char* readData = pm.pin(pid, LatchMode::SHARED);
    std::cout << "Read back: " << readData << std::endl;
    pm.unpin(pid, false, LatchMode::SHARED);

    // Deallocate page
    pm.deletePage(pid);
    pm.flushAll();
    return 0;
}
