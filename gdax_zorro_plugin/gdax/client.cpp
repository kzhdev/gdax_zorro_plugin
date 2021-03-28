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

    inline uint64_t get_timestamp() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    }

    inline uint64_t nonce() {
        static const uint64_t base = 1613437804467;
        return get_timestamp() - base;
    }

    inline double to_num_contracts(double lots, double qty_multiplier) {
        return ((lots / qty_multiplier) + 1e-11) * lots;
    }

    constexpr double epsilon = 1e-9;
    constexpr uint64_t default_norm_factor = 1e8;

    inline static double fix_floating_error(double value, int32_t norm_factor = default_norm_factor) {
        auto v = value + epsilon;
        return ((double)((int64_t)(v * norm_factor)) / norm_factor);
    }

    inline static uint32_t compute_number_decimals(double value) {
        value = std::abs(fix_floating_error(value));
        uint32_t count = 0;
        while (value - std::floor(value) > epsilon) {
            ++count;
            value *= 10;
        }
        return count;
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

    Client::Client(const std::string& key, const std::string& passphrase, const std::string& secret, bool isPaperTrading, const std::string& stp)
        : baseUrl_(isPaperTrading ? s_APIBaseURLPaper : s_APIBaseURLLive)
        , stp_(stp)
        , constant_headers_(
            "Content-Type:application/json\nUser-Agent:Zorro\nCB-ACCESS-KEY:" + key + 
            "\nCB-ACCESS-PASSPHRASE:" + passphrase)
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

    const std::unordered_map<std::string, Product>& Client::getProducts() {
        if (!products_.empty()) {
            return products_;
        }
        auto response =  request<std::vector<Product>>(baseUrl_ + "/products", public_api_headers_.c_str(), nullptr, &logger_, LogFilter::LF_PRODUCTS);
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

    const Product* Client::getProduct(const char* asset) {
        auto& products = getProducts();
        auto it = products.find(asset);
        if (it != products.end()) {
            return &it->second;
        }
        return nullptr;
    }

    Response<Ticker> Client::getTicker(const std::string& id) const {
        return request<Ticker>(baseUrl_ + "/products/" + id + "/ticker", public_api_headers_.c_str(), nullptr, &logger_, LogFilter::LF_TICKER);
    }

    Response<Time> Client::getTime() const {
        return request<Time>(baseUrl_ + "/time", public_api_headers_.c_str(), nullptr, &logger_, LogFilter::LF_TIME);
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

        uint32_t s;
        uint32_t e = end;
        nCandles *= n;

        auto download_candles = [this, &s, e, supported_granularity, AssetId, start]() {
            // make sure only 300 candles requests
            s = e - 300 * supported_granularity;
            if (s < start) {
                s = start;
            }

            std::stringstream ss;
            ss << baseUrl_ << "/products/" << AssetId << "/candles?start=" << timeToString(time_t(s)) << "&end=" << timeToString(time_t(e)) << "&granularity=" << supported_granularity;

            return request<Candles>(ss.str(), public_api_headers_.c_str(), nullptr, &logger_, LogFilter::LF_CANDLE);
        };

        if (n == 1) {
            auto rsp = download_candles();
            if (!rsp) {
                return rsp;
            }

            auto& downloadedCandles = rsp.content().candles;
            for (auto it = downloadedCandles.begin(); it != downloadedCandles.end();) {
                if (it->time > e || it->time < s) {
                    it = downloadedCandles.erase(it);
                }
                else {
                    ++it;
                }
            }
            return rsp;
        }

        Response<Candles> rt;
        auto& candles = rt.content().candles;
        candles.reserve(nCandles);
        uint32_t i = 0;
        Candle c;

        do {
            auto rsp = download_candles();
            if (!rsp) {
                BrokerError(rsp.what().c_str());
                break;
            }
            auto& downloadedCandles = rsp.content().candles;
            for (size_t i = 0; i < downloadedCandles.size(); ++i) {
                auto& candle = downloadedCandles[i];
                e = candle.time;
                if (candle.time > e) {
                    continue;
                }
                if (candle.time < s) {
                    break;
                }

                if (i == 0) {
                    c = candle;
                }
                else {
                    c.high = std::max<double>(candle.high, c.high);
                    c.low = std::min<double>(candle.low, c.low);
                    c.close = candle.close;
                    c.volume += candle.volume;
                }

                if (++i == n) {
                    candles.emplace_back(std::move(c));
                    i = 0;
                    --nCandles;
                }

                if (!nCandles) {
                    return rt;
                }
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

    Response<std::vector<Order>> Client::getOrders() const {

        std::string timestamp;
        std::string signature;
        if (sign("GET", "/orders?status=all", "", timestamp, signature)) {
            return request<std::vector<Order>>(baseUrl_ + "/orders?status=all", headers(signature, timestamp).c_str(), nullptr, &logger_, LogFilter::LF_ORDERS);
        }
        return Response<std::vector<Order>>(1, "Failed to sign /orders request");
    }

    Response<Order> Client::getOrder(const std::string& id) {
        auto it = filled_orders_.find(id);
        if (it != filled_orders_.end()) {
            Response<Order> response;
            response.content() = it->second;
            return response;
        }

        std::string timestamp;
        std::string signature;
        std::string path = "/orders/" + id;
        if (sign("GET", path, "", timestamp, signature)) {
            auto response = request<Order>(baseUrl_ + path, headers(signature, timestamp).c_str(), nullptr, &logger_, LogFilter::LF_ORDER);
            if (response && response.content().status == "done") {
                filled_orders_.emplace(id, response.content());
            }
            return response;
        }
        return Response<Order>(1, "Failed to sign /orders/" + id + " request");
    }

    Response<Fill> Client::getFill(const std::string& id) const {
        std::string timestamp;
        std::string signature;
        std::string path = "/fills?order_id=" + id;
        if (sign("GET", path, "", timestamp, signature)) {
            return request<Fill>(baseUrl_ + path, headers(signature, timestamp).c_str(), nullptr, &logger_, LogFilter::LF_FILL);
        }
        return Response<Fill>(1, "Failed to sign /fills?order_id=" + id + " request");
    }

    Response<Order> Client::submitOrder(
        const Product* product,
        const int lots,
        const OrderSide side,
        const OrderType type,
        const TimeInForce tif,
        double limit_price,
        double stop_price,
        bool post_only) {

        Response<Order> response;
        int32_t internalOrderId;
        int retry = 1;
        auto start = std::time(nullptr);
        do {
            rapidjson::StringBuffer s;
            s.Clear();
            rapidjson::Writer<rapidjson::StringBuffer> writer(s);
            writer.StartObject();

            writer.Key("product_id");
            writer.String(product->id.c_str());

            writer.Key("side");
            writer.String(to_string(side));

            writer.Key("type");
            writer.String(to_string(type));

            if (!stp_.empty()) {
                writer.Key("stp");
                writer.String(stp_.c_str());
            }

            std::ostringstream qty;
            qty.precision(compute_number_decimals(product->base_increment));
            qty << std::fixed << lots * product->base_min_size;

            if (type == OrderType::Limit) {
                writer.Key("time_in_force");
                writer.String(to_string(tif));

                writer.Key("price");
                writer.String(std::to_string(limit_price).c_str());

                if (stop_price) {
                    writer.Key("stop_price");
                    writer.String(std::to_string(stop_price).c_str());
                }
            }

            writer.Key("size");
            writer.String(qty.str().c_str());

            writer.EndObject();
            auto data = s.GetString();

            logger_.logTrace("SubmitOrder:\n%s\n", data);

            std::string timestamp;
            std::string signature;
            if (sign("POST", "/orders", data, timestamp, signature)) {
                response = request<Order>(baseUrl_ + "/orders", headers(signature, timestamp).c_str(), data, &logger_, LogFilter::LF_ORDER);
                if (response) {
                    Order* order = &response.content();
                    while (order->status == "pending") {
                        response = getOrder(order->id);
                        if (!response) {
                            break;
                        }
                        if (std::difftime(std::time(nullptr), start) >= 30) {
                            return Response<Order>(-2, "Order response timedout");
                        }
                        order = &response.content();
                    }
                    return response;
                }
            }
            else {
                response.onError(1, "Failed to sign order request");
                break;
            }            
        } while (!response && retry--);
        return response;
    }

    Response<Order> Client::cancelOrder(const std::string& id) {
        logger_.logDebug("--> DELETE %s/v2/orders/%s\n", baseUrl_.c_str(), id.c_str());
        auto response = request<Order>(baseUrl_ + "/v2/orders/" + id, "#DELETE", nullptr, &logger_, LogFilter::LF_ORDER);
        if (!response) {
            // Alpaca cancelOrder not return a object
            Order* order;
            do {
                auto resp = getOrder(id);
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

} // namespace gdax