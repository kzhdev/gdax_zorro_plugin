#pragma once

#include <string>
#include "rapidjson/document.h"

namespace gdax {

    struct Ticker {
        uint64_t trade_id = 0;
        double price = NAN;
        double size = 0;
        double bid = NAN;
        double ask = NAN;
        double volume = 0;
        std::string time;

    private:
        template <typename>
        friend class Response;

        template <typename T>
        std::pair<int, std::string> fromJSON(const T& parser) {
            parser.get<uint64_t>("trade_id", trade_id);
            parser.get<double>("price", price);
            parser.get<double>("size", size);
            parser.get<double>("ask", ask);
            parser.get<double>("bid", bid);
            parser.get<double>("volume", volume);
            parser.get<std::string>("time", time);
            return std::make_pair(0, "OK");
        }
    };

}
