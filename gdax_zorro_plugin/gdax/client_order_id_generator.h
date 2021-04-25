#pragma once

#include <windows.h>
#include <tchar.h>
#include <cstdint>
#include <atomic>
#include <thread>
#include <fstream>
#include <unordered_map>
#include "logger.h"
#include "gdax/client.h"

namespace gdax {

    /**
    * Zorro currently does not persist order's UUID, after restarting it will use an interger which Zorro maintained internally to
    * query trades. The plugin won't be able to relate Zorro's tradeId to any orders. 
    * 
    * The ClientOrderIdGenerator a helper class to generate a unique ClientOrderId
    * 
    * A ClientOrderId is composed from multiple parts as following:
    * 
    *     YYDDDNNNNN
    *     | |  |________ A incremental order counter [0 - 99999]
    *     | |_____________ Today's days of year (days since Jan 1 [0 - 365])
    *     |_______________ Years Since 2021
    * 
    * 
    * The id is stored in a shared memory as an atomic object. Therefore multiple Zorro-S instances can generate a unique id concurrently
    * 
    * The ClientOrderId is the TradeId turned to Zorro.
    * 
    */
    class ClientOrderIdGenerator {
        static constexpr const char* orderMapFile = "./Data/gdax.ord";
        static constexpr TCHAR szName[] = TEXT("GdaxOrderCounter");
        static constexpr uint32_t BF_SZ = 64;

        struct LockGuard {
            std::atomic_flag* lock_;

            LockGuard(std::atomic_flag* lock) : lock_(lock) {
                while (lock_->test_and_set(std::memory_order_acquire));
            }

            ~LockGuard() {
                lock_->clear(std::memory_order_release);
            }
        };

    public:
        ClientOrderIdGenerator(Client& client) {
            HANDLE hMapFile = CreateFileMapping(
                INVALID_HANDLE_VALUE,   // use paging file
                NULL,                   // default security
                PAGE_READWRITE,         // read/write access
                0,                      // maximum object size (high-order DWORD)
                BF_SZ,                  // maximum object size (low-order DWORD)
                szName                  // name of mapping object
            );

            bool own = false;
            if (hMapFile == NULL) {
                throw std::runtime_error("Failed to create shm. err=" + std::to_string(GetLastError()));
            }

            if (GetLastError() != ERROR_ALREADY_EXISTS) {
                own = true;
            }

            lpvMem_ = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, BF_SZ);
            if (!lpvMem_) {
                throw std::runtime_error("Failed to create shm. err=" + std::to_string(GetLastError()));
            }

            if (own) {
                // Retrive the last client order id from Alpaca
                lock_ = new (reinterpret_cast<uint8_t*>(lpvMem_) + 32) std::atomic_flag;
                LockGuard l(lock_);
                auto last_id = loadOrderMapping();
                if ((last_id / 100000) == (getBase() / 100000)) {
                    last_id = last_id % 100000;
                }
                else {
                    last_id = 0;
                }
                next_order_id_ = new (lpvMem_) std::atomic_int_fast32_t{ last_id };

                // clean up order id mapping file
                tidyUpOrderIdMappingFile();
            }
            else {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                lock_ = reinterpret_cast<std::atomic_flag*>(reinterpret_cast<uint8_t*>(lpvMem_) + 32);
                LockGuard l(lock_);
                loadOrderMapping();
                next_order_id_ = reinterpret_cast<std::atomic_int_fast32_t*>(lpvMem_);
            }
            auto next = next_order_id_->load(std::memory_order_relaxed);
            LOG_INFO("last_order_id_: id=%d\n", next);
        }

        ~ClientOrderIdGenerator() {
            if (lpvMem_) {
                UnmapViewOfFile(lpvMem_);
                lpvMem_ = nullptr;
            }

            if (hMapFile_) {
                CloseHandle(hMapFile_);
                hMapFile_ = nullptr;
            }
        }

        const char* getOrderUUID(int32_t client_oid) {
            auto it = mapOrderIds_.find(client_oid);
            if (it == mapOrderIds_.end()) {
                return nullptr;
            }
            return it->second.c_str();
        }

        void saveOrder(Order& order) {
            order.client_oid = nextOrderId();
            mapOrderIds_[order.client_oid] = order.id;
            persistOrderMapping(order.client_oid, order.id);
        }

        void onPositionClosed(int32_t client_oid) {
            persistOrderMapping(client_oid, "");
        }
            
    private:
        int32_t getBase() const noexcept {
            constexpr const uint32_t baseYear = 121;
            auto t = std::time(nullptr);
            struct tm now;
            localtime_s(&now, &t);
            return (now.tm_year - baseYear) * 100000000 + now.tm_yday * 100000;
        }

        int32_t nextOrderId() noexcept {
            constexpr const int32_t mask = 99999;   // Max order count allowed in single day is 99999.
            auto current = next_order_id_->load(std::memory_order_relaxed);
            auto next = current;
            do {
                next = (current + 1) & mask;
            } while (!next_order_id_->compare_exchange_weak(current, next, std::memory_order_release));
            return getBase() + next;
        }

        void persistOrderMapping(int32_t orderId, const std::string& uuid) {
            LockGuard l(lock_);
            std::ofstream f;
            f.open(orderMapFile, std::ios::app | std::ios::binary);
            if (!f.is_open()) {
                BrokerError(("Failed to open gdax.ord for writting. Order " + std::to_string(orderId) + " UUID: " + uuid + " need to be manually managed after Zorro restart").c_str());
                return;
            }

            f.write(reinterpret_cast<const char*>(&orderId), sizeof(int32_t));
            f.write(uuid.c_str(), 36);
            f.close();
        }

        int32_t loadOrderMapping() {
            std::ifstream f;
            f.open(orderMapFile, std::ios::binary);
            if (!f.is_open()) {
                BrokerError("Failed to open gdax.ord for reading");
                return 0;
            }

            int32_t last_id = 0;
            while (!f.eof() && f.good()) {
                int32_t client_oid;
                f.read(reinterpret_cast<char*>(&client_oid), sizeof(int32_t));
                last_id = std::max<int32_t>(client_oid, last_id);
                char uuid[36];
                f.read(&uuid[0], 36);
                if (uuid[0] == '\0') {
                    mapOrderIds_.erase(client_oid);
                }
                else {
                    mapOrderIds_.emplace(client_oid, std::string(uuid, 36));
                }
            }
            f.close();
            return last_id;
        }

        void tidyUpOrderIdMappingFile() {
            std::ofstream f;
            f.open(orderMapFile, std::ios::binary);
            if (!f.is_open()) {
                BrokerError("Failed to open gdax.ord for writting");
                return;
            }

            for (auto& kvp : mapOrderIds_) {
                //f << kvp.first << ":" << kvp.second << std::endl;
                f.write(reinterpret_cast<const char*>(&kvp.first), sizeof(int32_t));
                f.write(kvp.second.c_str(), 36);
            }
            f.close();
        }

    private:
        HANDLE hMapFile_ = nullptr;
        LPVOID lpvMem_ = nullptr;
        std::atomic_int_fast32_t* next_order_id_ = nullptr;
        std::atomic_flag* lock_ = nullptr;
        std::unordered_map<int32_t, std::string> mapOrderIds_;
    };
}