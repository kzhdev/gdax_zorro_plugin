#pragma once

#include "zorro_websocket_proxy_client.h"

namespace gdax {

    class GdaxWebsocket : public zorro::websocket::ZorroWebsocketProxyClient, public zorro::websocket::WebsocketProxyCallback {
        
        std::string key_;
        std::string phrase_;
        std::string secret_;
        std::string url_;

        struct Subscription {
            uint64_t tradeIndex{ 0 };
            uint64_t quoteIndex{ 0 };
        };

        std::shared_ptr<Subscription> subscribed_;
        std::unordered_map<std::string, std::shared_ptr<Subscription>> subscriptions_populator_;
        std::unordered_map<std::string, std::shared_ptr<Subscription>> subscriptions_reader_;

    public:
        GdaxWebsocket() : ZorroWebsocketProxyClient(this, "Gdax", BrokerError, BrokerProgress) {}
        ~GdaxWebsocket() override = default;

        bool login(const std::string& key, const std::string& phrase, const std::string& secret, bool isPractice) {
            key_ = key;
            phrase_ = phrase;
            secret_ = secret;
            //openWs();
            return true;
        }

        void logout() {

        }

        void onWebsocketProxyServerDisconnected() override {

        }

        void onWebsocketOpened(uint32_t id) override {

        }

        void onWebsocketClosed(uint32_t id) override {

        }

        void onWebsocketError(uint32_t id, const char* err, size_t len) override {

        }

        void onWebsocketData(uint32_t id, const char* data, size_t len, size_t remaining) {

        }

    };

}
