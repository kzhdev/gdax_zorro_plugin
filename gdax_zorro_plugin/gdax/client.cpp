#include "stdafx.h"
#include "gdax/client.h"

#include <sstream>
#include <memory>
#include <optional>
#include <chrono>
#include <algorithm>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "gdax/client_order_id_generator.h"

#include "cryptopp/cryptlib.h"
using CryptoPP::Exception;

#include "cryptopp/hmac.h"
using CryptoPP::HMAC;

#include "cryptopp/sha.h"
using CryptoPP::SHA256;

#include "cryptopp/base64.h"
using CryptoPP::Base64Encoder;
using CryptoPP::Base64Decoder;
using CryptoPP::byte;

#include "cryptopp/hex.h"
using CryptoPP::HexEncoder;

#include "cryptopp/filters.h"
using CryptoPP::StringSink;
using CryptoPP::StringSource;
using CryptoPP::HashFilter;

namespace {
    /// The base URL for API calls to the live trading API
    constexpr const char* s_APIBaseURLLive = "https://api.pro.coinbase.com";
    /// The base URL for API calls to the paper trading API
    constexpr const char* s_APIBaseURLPaper = "https://api-public.sandbox.pro.coinbase.com";
    std::unique_ptr<gdax::ClientOrderIdGenerator> s_orderIdGen;

    inline uint64_t get_timestamp() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    }

    inline uint64_t nonce() {
        static const uint64_t base = 1613437804467;
        return get_timestamp() - base;
    }
}

namespace gdax {
    std::string timeToString(time_t time) {
        tm* tm;
        tm = gmtime(&time);
        char buf[64];
        strftime(buf, 64, "%Y-%m-%dT%H:%M:%SZ", tm);
        return buf;
    }

    Client::Client(std::string key, std::string passphrase, std::string secret, bool isPaperTrading)
        : baseUrl_(isPaperTrading ? s_APIBaseURLPaper : s_APIBaseURLLive)
        , constant_headers_(
            "Content-Type:application/json\nUser-Agent:Zorro/2.3\nCB-ACCESS-KEY:" + std::move(key) + 
            "\nCB-ACCESS-PASSPHRASE:" + std::move(passphrase))
        , public_api_headers_("User-Agent:Zorro")
        , isLiveMode_(!isPaperTrading)
    {
        try {
            StringSource(secret, true, new Base64Decoder(new StringSink(secret_)));
        }
        catch (const CryptoPP::Exception& e) {
            BrokerError(("failed to decode API secret. err=" + std::string(e.what())).c_str());
            throw std::runtime_error(e.what());
        }
        //s_orderIdGen = std::make_unique<ClientOrderIdGenerator>(*this);
    }

    bool Client::sign(
        const char* method,
        const std::string& request_path,
        const std::string& body,
        std::string& timestamp,
        std::string& sign) const {
        try {
            timestamp = std::to_string(get_timestamp());
            std::string msg = timestamp;
            msg.append(method).append(request_path).append(body);
            std::string mac;
            HMAC<SHA256> hmac((unsigned char*)secret_.c_str(), secret_.size());
            StringSource(msg, true, new HashFilter(hmac, new StringSink(mac)));
            StringSource(mac, true, new Base64Encoder(new StringSink(sign), false));
            return true;
        }
        catch (const CryptoPP::Exception& e) {
            BrokerError(("failed to sign request. err=" + std::string(e.what())).c_str());
        }
        return false;
    }

    std::string Client::headers(const std::string& sign, const std::string& timestamp) const {
        std::stringstream ss;
        ss << constant_headers_ << "\nCB-ACCESS-SIGN:" << sign << "\nCB-ACCESS-TIMESTAMP:" << timestamp;
        //logger_.logDebug("%s\n", ss.str().c_str());
        return ss.str();
    }

    Response<std::vector<Account>> Client::getAccounts() const {
        std::string timestamp;
        std::string signature;
        if (sign("GET", "/accounts", "", timestamp, signature)) {
            return request<std::vector<Account>>(baseUrl_ + "/accounts", headers(signature, timestamp).c_str(), nullptr, &logger_);
        }
        return Response<std::vector<Account>>(1, "Failed to sign /accounts request");
    }

    const std::unordered_map<std::string, Product>& Client::getProducts() const {
        if (!products_.empty()) {
            return products_;
        }
        auto response =  request<std::vector<Product>>(baseUrl_ + "/products", public_api_headers_.c_str(), nullptr, &logger_);
        if (!response) {
            BrokerError(("Failed to get products. err=" + response.what()).c_str());
        }
        else {
            for (auto& prod : response.content()) {
                products_.insert(std::make_pair(prod.id, prod));
            }
        }
        return products_;
    }

    Response<Ticker> Client::getTicker(const std::string& id) const {
        return request<Ticker>(baseUrl_ + "/products/" + id + "/ticker", public_api_headers_.c_str(), nullptr, &logger_);
    }

    Response<Time> Client::getTime() const {
        return request<Time>(baseUrl_ + "/time", public_api_headers_.c_str(), nullptr, &logger_);
    }

    Response<Candles> Client::getCandles(const std::string& AssetId, uint32_t start, uint32_t end, uint32_t granularity, uint32_t nCandles) const {
        static std::vector<uint32_t> s_valid_granularity = { 60, 300, 900, 3600, 21600, 86400 };
        auto iter = std::find(s_valid_granularity.begin(), s_valid_granularity.end(), granularity);

        uint32_t supported_granularity = granularity;
        uint32_t n = 1;

        if (iter == s_valid_granularity.end()) {
            // not a supported granularity;
            assert(false);
            bool find = false;
            for (auto it = s_valid_granularity.rbegin(); it != s_valid_granularity.rend(); ++it) {
                if ((granularity % (*it)) == 0) {
                    supported_granularity = *it;
                    n = (uint32_t)(granularity / supported_granularity);
                    logger_.logDebug("Grenularity %n is not supported by Coinbase Pro, use %d instead.", granularity, supported_granularity);
                    find = true;
                    break;
                }
            }

            if (!find) {
                return Response<Candles>(1, "Granularity " + std::to_string(granularity) + " is not supported");
            }
        }

        // make sure only 300 candles requests
        uint32_t s;
        uint32_t e = end;
        nCandles *= n;

        Response<Candles> rt;
        auto& candles = rt.content().candles;
        candles.reserve(nCandles);
        uint32_t i = 0;
        Candle c;

        do {
            s = e - 300 * granularity;
            if (s < start) {
                s = start;
            }

            std::stringstream ss;
            ss << baseUrl_ << "/products/" << AssetId << "/candles?start=" << timeToString(time_t(s)) << "&end=" << timeToString(time_t(e)) << "&granularity" << supported_granularity;

            auto rsp = request<Candles>(ss.str(), public_api_headers_.c_str(), nullptr, &logger_);
            if (!rsp) {
                BrokerError(rsp.what().c_str());
                break;
            }

            auto& downloadedCandles = rsp.content().candles;
            auto it = downloadedCandles.begin();
            while (it != downloadedCandles.end()) {
                auto& candle = downloadedCandles.front();
                e = candle.time;
                if (candle.time > e) {
                    it = downloadedCandles.erase(it);
                    continue;
                }
                if (candle.time < s) {
                    break;
                }
                
                if (n > 1) {
                    if (i == 0) {
                        c = candle;
                    }
                    else {
                        c.high = std::max<double>(candle.high, c.high);
                        c.low = std::min<double>(candle.low, c.low);
                        c.close = candle.close;
                        c.volume = candle.volume;
                    }
                    
                    if (++i = n) {
                        candles.emplace_back(std::move(c));
                        i = 0;
                        --nCandles;
                    }
                }
                else {
                    candles.emplace_back(candle);
                    --nCandles;
                }
                it = downloadedCandles.erase(it);
            }
            e -= 30;
        } while (nCandles > 0 && s > start);
        return rt;
    }

    //Response<std::vector<Trade>> Client::getTrades(const std::string& AssetId) const {
    //    //std::stringstream url;
    //    //url << baseUrl_ << "/v1/trades/" << symbol << "?timestamp=" << timestamp << "&limit_trades=" << limit; /**/
    //    return request<std::vector<Trade>>(url.str(), nullptr);
    //}

    Response<std::vector<Order>> Client::getOrders(
        ActionStatus status,
        int limit,
        const std::string& after,
        const std::string& until,
        OrderDirection direction,
        bool nested) const {
        std::vector<std::string> queries;
        if (status != ActionStatus::Open) {
            queries.emplace_back("status=" + std::string(to_string(status)));
        }
        if (limit != 50) {
            queries.emplace_back("limit=" + std::to_string(limit));
        }
        if (!after.empty()) {
            queries.emplace_back("after=" + after);
        }
        if (!until.empty()) {
            queries.emplace_back("until=" + until);
        }
        if (direction != OrderDirection::Descending) {
            queries.emplace_back("direction=" + std::string(to_string(direction)));
        }
        if (nested) {
            queries.emplace_back("nested=1");
        }

        std::stringstream url;
        url << baseUrl_ << "/v2/orders";
        if (!queries.empty()) {
            url << "?";
            int32_t i = 0;
            for (; i < (int32_t)queries.size() - 1; ++i) {
                url << queries[i] << '&';
            }
            url << queries[i];
        }
        logger_.logDebug("--> %s\n", url.str().c_str());
        return request<std::vector<Order>>(url.str());
    }

    Response<Order> Client::getOrder(const std::string& id, const bool nested, const bool logResponse) const {
        auto url = baseUrl_ + "/v2/orders/" + id;
        if (nested) {
            url += "?nested=true";
        }

        Response<Order> response;
        if (logResponse) {
            return request<Order>(url, nullptr, nullptr, &logger_);
        }
        return request<Order>(url);
    }

    Response<Order> Client::getOrderByClientOrderId(const std::string& clientOrderId) const {
        return request<Order>(baseUrl_ + "/v2/orders:by_client_order_id?client_order_id=" + clientOrderId);
    }

    Response<Order> Client::submitOrder(
        const std::string& symbol,
        const int quantity,
        const OrderSide side,
        const OrderType type,
        const TimeInForce tif,
        const std::string& limit_price,
        const std::string& stop_price,
        bool extended_hours,
        const std::string& client_order_id,
        const OrderClass order_class,
        TakeProfitParams* take_profit_params,
        StopLossParams* stop_loss_params) const {
        
        if (!is_open_ && !extended_hours) {
            return Response<Order>(1, "Market Close.");
        }

        Response<Order> response;
        int32_t internalOrderId;
        int retry = 1;
        do {
            rapidjson::StringBuffer s;
            s.Clear();
            rapidjson::Writer<rapidjson::StringBuffer> writer(s);
            writer.StartObject();

            writer.Key("symbol");
            writer.String(symbol.c_str());

            writer.Key("qty");
            writer.Int(quantity);

            writer.Key("side");
            writer.String(to_string(side));

            writer.Key("type");
            writer.String(to_string(type));

            writer.Key("time_in_force");
            writer.String(to_string(tif));

            if (!limit_price.empty()) {
                writer.Key("limit_price");
                writer.String(limit_price.c_str());
            }

            if (!stop_price.empty()) {
                writer.Key("stop_price");
                writer.String(stop_price.c_str());
            }

            if (extended_hours) {
                writer.Key("extended_hours");
                writer.Bool(extended_hours);
            }

            internalOrderId = s_orderIdGen->nextOrderId();
            std::stringstream clientOrderId;
            clientOrderId << "ZORRO_";
            if (!client_order_id.empty()) {
                if (client_order_id.size() > 32) {  // Alpaca client order id max length is 48
                    clientOrderId << client_order_id.substr(0, 32);
                }
                else {
                    clientOrderId << client_order_id;
                }
                clientOrderId << "_";
            }

            clientOrderId << internalOrderId;
            writer.Key("client_order_id");
            writer.String(clientOrderId.str().c_str());

            if (order_class != OrderClass::Simple) {
                writer.Key("order_class");
                writer.String(to_string(order_class));
            }

            if (take_profit_params != nullptr) {
                writer.Key("take_profit");
                writer.StartObject();
                if (take_profit_params->limitPrice != "") {
                    writer.Key("limit_price");
                    writer.String(take_profit_params->limitPrice.c_str());
                }
                writer.EndObject();
            }

            if (stop_loss_params != nullptr) {
                writer.Key("stop_loss");
                writer.StartObject();
                if (!stop_loss_params->limitPrice.empty()) {
                    writer.Key("limit_price");
                    writer.String(stop_loss_params->limitPrice.c_str());
                }
                if (!stop_loss_params->stopPrice.empty()) {
                    writer.Key("stop_price");
                    writer.String(stop_loss_params->stopPrice.c_str());
                }
                writer.EndObject();
            }

            writer.EndObject();
            auto data = s.GetString();

            logger_.logDebug("--> POST %s/v2/orders\n", baseUrl_.c_str());
            if (data) {
                logger_.logTrace("Data:\n%s\n", data);
            }
            response = request<Order>(baseUrl_ + "/v2/orders", data, nullptr, &logger_);
            if (!response && response.what() == "client_order_id must be unique") {
                // clinet order id has been used.
                // increment conflict count and try again.
                s_orderIdGen->onIdConflict();
            }
        } while (!response && retry--);

        assert(!response || response.content().internal_id == internalOrderId);
        return response;
    }

    Response<Order> Client::replaceOrder(
        const std::string& id,
        const int quantity,
        const TimeInForce tif,
        const std::string& limit_price,
        const std::string& stop_price,
        const std::string& client_order_id) const {

        rapidjson::StringBuffer s;
        s.Clear();
        rapidjson::Writer<rapidjson::StringBuffer> writer(s);
        writer.StartObject();

        writer.Key("qty");
        writer.String(std::to_string(quantity).c_str());

        writer.Key("time_in_force");
        writer.String(to_string(tif));

        if (limit_price != "") {
            writer.Key("limit_price");
            writer.String(limit_price.c_str());
        }

        if (stop_price != "") {
            writer.Key("stop_price");
            writer.String(stop_price.c_str());
        }

        auto internalOrderId = s_orderIdGen->nextOrderId();
        std::stringstream clientOrderId;
        clientOrderId << "ZORRO_";
        if (!client_order_id.empty()) {
            if (client_order_id.size() > 32) {  // Alpaca client order id max length is 48
                clientOrderId << client_order_id.substr(0, 32);
            }
            else {
                clientOrderId << client_order_id;
            }
            clientOrderId << "_";
        }

        clientOrderId << internalOrderId;
        writer.Key("client_order_id");
        writer.String(clientOrderId.str().c_str());

        writer.EndObject();
        std::string body("#PATCH ");
        body.append(s.GetString());

        logger_.logDebug("--> %s/v2/orders/%s\n", baseUrl_.c_str(), id.c_str());
        logger_.logTrace("Data:\n%s\n", body.c_str());
        return request<Order>(baseUrl_ + "/v2/orders/" + id, body.c_str(), nullptr, &logger_);
    }

    Response<Order> Client::cancelOrder(const std::string& id) const {
        logger_.logDebug("--> DELETE %s/v2/orders/%s\n", baseUrl_.c_str(), id.c_str());
        auto response = request<Order>(baseUrl_ + "/v2/orders/" + id, "#DELETE", nullptr, &logger_);
        if (!response) {
            // Alpaca cancelOrder not return a object
            Order* order;
            do {
                auto resp = getOrder(id, false, true);
                if (resp) {
                    order = &resp.content();
                    if (order->status == "canceled" || order->status == "filled") {
                        return resp;
                    }
                }
            } while (order->status == "pending_cancel");
            logger_.logWarning("failed to cancel order %s. order status=%s", id.c_str(), order->status.c_str());
            return Response<Order>(1, "Failed to cancel order");
        }
        return response;
    }

    Response<Position> Client::getPosition(const std::string& symbol) const {
        return request<Position>(baseUrl_ + "/v2/positions/" + symbol, nullptr, nullptr, &logger_);
    }
} // namespace gdax