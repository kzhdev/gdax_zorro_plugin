#pragma once

#include <cstdint>
#include <string>
#include "order.h"

namespace gdax {

    struct Fill {
        std::string product_id;
        std::string order_id;
        std::string created_at;
        double price;
        double size;
        double fee;
        uint32_t fill_id;
        OrderSide side;
        bool settled;



    private:
        template <typename> friend class Response;

        template <typename T>
        std::pair<int, std::string> fromJSON(const T& parser) {
            parser.get<std::string>("product_id", product_id);
            parser.get<std::string>("order_id", order_id);
            parser.get<std::string>("created_at", created_at);
            parser.get<double>("price", price);
            parser.get<double>("size", size);
            parser.get<double>("fee", fee);
            parser.get<bool>("settled", settled);
            parser.get<uint32_t>("fill_id", fill_id);
            side = to_orderSide(parser.get<std::string>("side"));
            return std::make_pair(0, "OK");
        }
    };
}