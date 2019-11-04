#include "nvm_mgr.h"

#include <cassert>
#include <mutex>
#include <stdio.h>

namespace NVMMgr_ns {

// global nvm manager
NVMMgr *nvm_mgr = NULL;
std::mutex _mtx;

int create_file(const char *file_name, uint64_t file_size) {
    std::ofstream fout(file_name);
    if (fout) {
        fout.close();
        int result = truncate(file_name, file_size);
        if (result != 0) {
            printf("[NVM MGR]\ttruncate new file failed\n");
            exit(1);
        }
    } else {
        printf("[NVM MGR]\tcreate new file failed\n");
        exit(1);
    }

    return 0;
}

NVMMgr::NVMMgr() {
    // access 的返回结果， 0: 存在， 1: 不存在
    int initial = access(get_filename(), F_OK);

    if (initial) {
        int result = create_file(get_filename(), filesize);
        if (result != 0) {
            printf("[NVM MGR]\tcreate file failed when initalizing\n");
            exit(1);
        }
        printf("[NVM MGR]\tcreate file success.\n");
    }

    // open file
    fd = open(get_filename(), O_RDWR);
    if (fd < 0) {
        printf("[NVM MGR]\tfailed to open nvm file\n");
        exit(-1);
    }
    if (ftruncate(fd, filesize) < 0) {
        printf("[NVM MGR]\tfailed to truncate file\n");
        exit(-1);
    }
    printf("[NVM MGR]\topen file %s success.\n", get_filename());

    // mmap
    void *addr = mmap((void *)start_addr, filesize, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);

    if (addr != (void *)start_addr) {
        printf("[NVM MGR]\tmmap failed %p \n", addr);
        exit(0);
    }
    printf("[NVM MGR]\tmmap successfully\n");

    // initialize meta data
    meta_data = static_cast<Head *>(addr);
    if (initial) {
        // set status of head and set zero for bitmap
        // persist it
        memset((void *)meta_data, 0, PGSIZE);

        meta_data->status = magic_number;
        meta_data->threads = 0;
        free_bit_offset = 0;

        flush_data((void *)meta_data, PGSIZE);
        printf("[NVM MGR]\tinitialize nvm file's head\n");
    }
    free_page_list.clear();

    reload_free_blocks();
}

NVMMgr::~NVMMgr() {
    // normally exit
    printf("[NVM MGR]\tnormally exits, NVM reset..\n");
    //        Head *head = (Head *) start_addr;
    //        flush_data((void *) head, sizeof(Head));
    munmap((void *)start_addr, filesize);
    close(fd);
}

//    void NVMMgr::recover_done() {
//        Head *head = (Head *) start_addr;
//        head->threads = 0;
//        flush_data((void *) head, sizeof(Head));
//    }

bool NVMMgr::reload_free_blocks() {
    assert(free_page_list.empty());

    while (true) {
        if (free_bit_offset >= (filesize / PGSIZE) - (max_threads + 1)) {
            return false;
        }

        uint8_t value = meta_data->bitmap[free_bit_offset];

        // not free
        if (value != 0) {
            free_bit_offset++;
            continue;
        } else if (value == 0) { // free
            for (int i = 0; i < 8; i++) {
                if (free_bit_offset >=
                    (filesize / PGSIZE) - (max_threads + 1)) {
                    break;
                }
                if (meta_data->bitmap[free_bit_offset] != 0) {
                    free_bit_offset++;
                    continue;
                }

                free_page_list.push_back(free_bit_offset);
                free_bit_offset++;
            }
        }
        break;
    }
    std::cout << "[NVM MGR]\treload free blocks, now free_page_list size is "
              << free_page_list.size() << "\n";
    return true;
}

void *NVMMgr::alloc_thread_info() {
    // not thread safe
    size_t index = meta_data->threads++;
    flush_data((void *)&(meta_data->threads), sizeof(int));
    return (void *)(thread_local_start + index * PGSIZE);
}

void *NVMMgr::alloc_block(int type) {
    std::lock_guard<std::mutex> lock(_mtx);

    if (free_page_list.empty()) {
        if (!reload_free_blocks()) {
            return NULL;
        }
    }
    uint64_t id = free_page_list.front();
    void *addr = (void *)(data_block_start + id * PGSIZE);
    free_page_list.pop_front();

    meta_data->bitmap[id] = type;
    flush_data((void *)&(meta_data->bitmap[id]), sizeof(int));
    printf("[NVM MGR]\talloc a new block %d, type is %d\n", id, type);

    // TODO: crash consistency allocation to avoid memory leak

    return addr;
}

/*
 * interface to call methods of nvm_mgr
 */
NVMMgr *get_nvm_mgr() {
    std::lock_guard<std::mutex> lock(_mtx);

    if (nvm_mgr == NULL) {
        printf("[NVM MGR]\tnvm manager is not initilized.\n");
        assert(0);
    }
    return nvm_mgr;
}

bool init_nvm_mgr() {
    std::lock_guard<std::mutex> lock(_mtx);

    if (nvm_mgr) {
        printf("[NVM MGR]\tnvm manager has already been initilized.\n");
        return false;
    }
    nvm_mgr = new NVMMgr();
    return true;
}

void close_nvm_mgr() {
    std::lock_guard<std::mutex> lock(_mtx);

    if (nvm_mgr != NULL) {
        delete nvm_mgr;
        nvm_mgr = NULL;
    }
}
} // namespace NVMMgr_ns
