#pragma once

#include <string>
#include <cassert>
#include <unordered_map>
#include "rapidjson/document.h"

namespace gdax {

    /**
     * @brief When you submit an order, you may be buying or selling.
     */
    enum OrderSide : uint8_t {
        Buy,
        Sell,
    };

    /**
     * @brief A helper to convert an OrderSide to a string
     */
    inline constexpr const char* to_string(OrderSide side) {
        constexpr const char* sOrderSide[] = { "buy", "sell" };
        assert(side >= OrderSide::Buy && side <= OrderSide::Sell);
        return sOrderSide[side];
    }

    inline OrderSide to_orderSide(const std::string& side) {
        return side == "buy" ? OrderSide::Buy : OrderSide::Sell;
    }

    /**
     * @brief When you submit an order, you can choose a supported order type.
     *
     * For more information on supported order types, see:
     * https://alpaca.markets/docs/trading-on-alpaca/orders/#order-types
     */
    enum OrderType : uint8_t {
        Market,
        Limit,
    };

    /**
     * @brief A helper to convert an OrderType to a string
     */
    inline constexpr const char* to_string(OrderType type)
    {
        constexpr const char* sOrderType[] = { "market", "limit" };
        assert(type >= OrderType::Market && type <= OrderType::Limit);
        return sOrderType[type];
    }

    inline OrderType to_orderType(const std::string& type) {
        static std::unordered_map<std::string, OrderType> orderTypes = {
            {"market", OrderType::Market},
            {"limit", OrderType::Limit},
        };
        assert(orderTypes.find(type) != orderTypes.end());
        return orderTypes[type];
    }

    /**
     * @brief Alpaca supports several Time-In-Force designations
     *
     * For more information on the supported designations, see:
     * https://alpaca.markets/docs/trading-on-alpaca/orders/#time-in-force
     */
    enum TimeInForce : uint8_t {
        GTC,
        GTT,
        IOC,
        FOK,
    };

    /**
     * @brief A helper to convert an OrderTimeInForce to a string
     */
    inline constexpr const char* to_string(TimeInForce tif) {
        constexpr const char* sTIF[] = { "GTC", "GTT", "IOC", "FOK" };
        assert(tif >= TimeInForce::GTC && tif <= TimeInForce::FOK);
        return sTIF[tif];
    }

    inline TimeInForce to_timeInForce(const std::string& tif) {
        static std::unordered_map<std::string, TimeInForce> tifs = {
            {"GTC", TimeInForce::GTC},
            {"GTT", TimeInForce::GTT},
            {"IOC", TimeInForce::IOC},
            {"FOK", TimeInForce::FOK},
        };
        assert(tifs.find(tif) != tifs.end());
        return tifs[tif];
    }

    enum StopType : uint8_t {
        Loss,
        Entry,
    };

    inline constexpr const char* to_string(StopType stopType) {
        constexpr const char* sStopType[] = { "loss", "entry" };
        assert(stopType >= StopType::Loss && stopType <= StopType::Entry);
        return sStopType[stopType];
    }

    inline StopType to_stopType(const std::string& type) {
        static std::unordered_map<std::string, StopType> stopTypes = {
            {"loss", StopType::Loss},
            {"entry", StopType::Entry},
        };
        assert(stopTypes.find(type) != stopTypes.end());
        return stopTypes[type];
    }


    /**
     * @brief A type representing an Alpaca order.
     */
    struct Order {
        double price = NAN;
        double size = 0.;
        double fill_fees;
        double filled_size;
        double filled_price;
        double executed_value;
        OrderSide side;
        TimeInForce tif = TimeInForce::GTC;
        OrderType type;
        bool post_only = false;
        bool settled = false;

        std::string product_id;
        std::string created_at;
        std::string id;
        std::string stp;
        std::string status;

    private:
        template<typename> friend class Response;

        template<typename T>
        std::pair<int, std::string> fromJSON(const T& parser) {
            parser.get<std::string>("id", id);
            parser.get<std::string>("created_at", created_at);
            parser.get<std::string>("product_id", product_id);
            parser.get<std::string>("stp", stp);
            parser.get<double>("price", price);
            parser.get<double>("size", size);
            type = to_orderType(parser.get<std::string>("type"));
            side = to_orderSide(parser.get<std::string>("side"));
            if (parser.json.HasMember("time_in_force")) {
                tif = to_timeInForce(parser.get<std::string>("time_in_force"));
            }
            parser.get<double>("filled_size", filled_size);
            parser.get<double>("fill_fees", fill_fees);
            parser.get<double>("executed_value", executed_value);
            parser.get<std::string>("status", status);
            parser.get<bool>("post_only", post_only);
            parser.get<bool>("settled", settled);
            if (filled_size) {
                filled_price = executed_value / filled_size;
            }
            return std::make_pair(0, "OK");
        }
    };
} // namespace gdax