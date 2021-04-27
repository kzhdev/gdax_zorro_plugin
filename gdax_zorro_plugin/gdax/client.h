#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "request.h"
#include "gdax/account.h"
#include "gdax/product.h"
#include "gdax/candle.h"
#include "gdax/order.h"
#include "gdax/ticker.h"
#include "gdax/fill.h"
#include "gdax/time.h"

namespace gdax {

    class MarketData;

    std::string timeToString(time_t time);

    class Client final {
    public:
        explicit Client() = delete;
        explicit Client(const std::string& key, const std::string& passphrase, const std::string& secret, bool isPaperTrading, const std::string& stp = "");
        ~Client() = default;

        bool isLiveMode() const noexcept { return isLiveMode_;  }

        Response<std::vector<Account>> getAccounts() const;

        const std::unordered_map<std::string, Product>& getProducts();
        const Product* getProduct(const char* asset);

        Response<Ticker> getTicker(const std::string& id) const;

        Response<Time> getTime() const;

        Response<Candles> getCandles(const std::string& AssetId, uint32_t start, uint32_t end, uint32_t granulairty, uint32_t nCandles) const;
        //Response<std::vector<Trade>> getTrades(const std::string& AssetId) const;
            
        Response<std::vector<Order>> getOrders() const;

        Response<Order*> getOrder(const std::string& order_id);

        Response<Order*> submitOrder(
            const Product* const product,
            double lots,
            OrderSide side,
            OrderType type,
            TimeInForce tif,
            double limit_price = 0.0,
            double stop_price = 0.0,
            bool post_only = false);

        Response<bool> cancelOrder(Order& order);

        const char* getOrderUUID(int32_t client_oid);
        void onPositionClosed(int32_t client_oid);

    private:
        bool sign(
            const char* method,
            const std::string& request_path,
            std::string& timestamp,
            std::string& sign,
            const std::string& body = "") const;
        std::string headers(const std::string& sign, const std::string& timestamp) const;

        Response<Order*> getOrder(Order*);

    private:
        const std::string baseUrl_;
        std::string secret_;
        std::string stp_;
        const std::string constant_headers_;
        const std::string public_api_headers_;
        //mutable bool is_open_ = false;
        const bool isLiveMode_;

        std::unordered_map<std::string, Product> products_;
        std::unordered_map<std::string, Order> orders_;
    };

} // namespace gdax