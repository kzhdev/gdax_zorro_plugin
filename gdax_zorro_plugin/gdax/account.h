#pragma once

#include <string>
#include "rapidjson/document.h"

namespace gdax {

    /**
     * @brief A type representing an Alpaca account.
     */
    struct Account {
        std::string id;
        std::string currency;
        std::string profile_id;
        double balance;
        double available;
        double hold;
        bool trading_enabled;

    private:
        template<typename> friend class Response;

        template<typename parserT>
        std::pair<int, std::string> fromJSON(const parserT& parser) {
            parser.get<std::string>("id", id);
            parser.get<std::string>("currency", currency);
            parser.get<std::string>("profile_id", profile_id);
            parser.get<double>("balance", balance);
            parser.get<double>("available", available);
            parser.get<double>("hold", hold);
            parser.get<bool>("trading_enabled", trading_enabled);
            return std::make_pair(0, "OK");
        }
    };

} // namespace gdax