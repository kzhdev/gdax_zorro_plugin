#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "request.h"
#include "logger.h"
#include "gdax/account.h"
#include "gdax/product.h"
#include "market_data/candle.h"
#include "market_data/quote.h"
#include "gdax/order.h"
#include "gdax/position.h"
#include "gdax/ticker.h"
#include "gdax/trade.h"
#include "gdax/time.h"

namespace gdax {

    class MarketData;

    std::string timeToString(time_t time);

    class Client final {
    public:
        explicit Client() = delete;
        explicit Client(std::string key, std::string passphrase, std::string secret, bool isPaperTrading);
        ~Client() = default;

        Logger& logger() noexcept { return logger_;  }

        bool isLiveMode() const noexcept { return isLiveMode_;  }

        Response<std::vector<Account>> getAccounts() const;

        const std::unordered_map<std::string, Product>& getProducts() const;
        Response<Ticker> getTicker(const std::string& id) const;

        Response<Time> getTime() const;

        Response<Candles> getCandles(const std::string& AssetId, uint32_t start, uint32_t end, uint32_t granulairty, uint32_t nCandles) const;
        //Response<std::vector<Trade>> getTrades(const std::string& AssetId) const;
            
        Response<std::vector<Order>> getOrders(
            const ActionStatus status = ActionStatus::Open,
            const int limit = 50,
            const std::string& after = "",
            const std::string& until = "",
            const OrderDirection = OrderDirection::Descending,
            const bool nested = false) const;

        Response<Order> getOrder(const std::string& id, const bool nested = false, const bool logResponse = false) const;
        Response<Order> getOrderByClientOrderId(const std::string& clientOrderId) const;

        Response<Order> submitOrder(
            const std::string& symbol,
            const int quantity,
            const OrderSide side,
            const OrderType type,
            const TimeInForce tif,
            const std::string& limit_price = "",
            const std::string& stop_price = "",
            bool extended_hours = false,
            const std::string& client_order_id = "",
            const OrderClass order_class = OrderClass::Simple,
            TakeProfitParams* take_profit_params = nullptr,
            StopLossParams* stop_loss_params = nullptr) const;

        Response<Order> replaceOrder(
            const std::string& id,
            const int quantity,
            const TimeInForce tif,
            const std::string& limit_price = "",
            const std::string& stop_price = "",
            const std::string& client_order_id = "") const;

        Response<Order> cancelOrder(const std::string& id) const;

        Response<Position> getPosition(const std::string& symbol) const;

    private:
        bool sign(
            const char* method,
            const std::string& request_path,
            const std::string& body,
            std::string& timestamp,
            std::string& sign) const;
        std::string headers(const std::string& sign, const std::string& timestamp) const;

    private:
        const std::string baseUrl_;
        std::string secret_;
        const std::string constant_headers_;
        const std::string public_api_headers_;
        mutable bool is_open_ = false;
        const bool isLiveMode_;
        mutable Logger logger_;

        mutable std::unordered_map<std::string, Product> products_;
    };

} // namespace gdax