#pragma once

#include <windows.h>
#include <tchar.h>
#include <cstdint>
#include <atomic>
#include <functional>
#include <thread>
#include "logger.h"
#include "alpaca/client.h"
#include "market_data/quote.h"
#include "slicksocket/websocket_client.h"
#include "slick_queue.h"

namespace alpaca {

    class Websocket;
    class AlpacaWebsocket;
    class PolygonWebsocket;

    class WebsocketWorker {
        static constexpr uint32_t BF_SZ = std::max<size_t>(sizeof(AlpacaWebsocket), sizeof(PolygonWebsocket)) + 64;
    public:
        WebsocketWorker(Logger& logger, const TCHAR* name, std::function<void()>&& runFunc) : logger_(logger) {
            HANDLE hMapFile = CreateFileMapping(
                INVALID_HANDLE_VALUE,               // use paging file
                NULL,                               // default security
                PAGE_READWRITE,                     // read/write access
                0,                                  // maximum object size (high-order DWORD)
                BF_SZ,                              // maximum object size (low-order DWORD)
                name                                // name of mapping object
            );

            bool own = false;
            auto err = GetLastError();
            if (hMapFile == NULL) {
                throw std::runtime_error("Failed to create shm. err=" + std::to_string(err));
            }

            if (err != ERROR_ALREADY_EXISTS) {
                own = true;
                logger_.logInfo("shm created\n");
            }
            else {
                logger_.logInfo("shm opened\n");
            }

            lpvMem_ = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, BF_SZ);
            if (!lpvMem_) {
                throw std::runtime_error("Failed to create shm. err=" + std::to_string(GetLastError()));
            }

            if (own) {
                websocket_ = new (lpvMem_)Websocket();
            }
            else {
                websocket_ = reinterpret_cast<Websocket*>(lpvMem_);
            }
        }

        ~WebsocketWorker() {
            if (lpvMem_) {
                UnmapViewOfFile(lpvMem_);
                lpvMem_ = nullptr;
            }

            if (hMapFile_) {
                CloseHandle(hMapFile_);
                hMapFile_ = nullptr;
            }
        }

    private:
        HANDLE hMapFile_ = nullptr;
        LPVOID lpvMem_ = nullptr;
        Logger& logger_;
        Websocket* websocket_ = nullptr;
    };


    class Websocket : public slick::net::websocket_callback {
    public:
        Websocket() : logger_("alpaca_ws") {}

        virtual ~Websocket() = default;

        void on_connected() override {
            logger_.logInfo("websocket connected");
        }

        void on_disconnected() override {
            logger_.logInfo("websocket disconnected");
        }

        void on_error(const char* msg, size_t len) override {
            if (msg && len) {
                logger_.logError("OnError %.*s", len, msg);
            }
            else {
                logger_.logError("Unkown error occurred");
            }

        }

        void on_data(const char* data, size_t len, size_t remaining) override {

        }

    protected:
        Logger logger_;
        std::thread thread_;
        slick::net::websocket_client websocket_;
        slick::SlickQueue<char, > buffer_;
    };

    class AlpacaWebsocket : public Websocket {
    public:
        AlpacaWebsocket() = default;
        ~AlpacaWebsocket() override = default;
    };

    class PolygonWebsocket : public Websocket {
    public:
        PolygonWebsocket() = default;
        ~PolygonWebsocket() override = default;
    };
}