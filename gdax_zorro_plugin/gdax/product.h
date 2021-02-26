#pragma once

#include <string>
#include <cassert>
#include <unordered_map>

namespace gdax {

	struct Product {
		std::string id;
		std::string display_name;
		std::string status;
		std::string status_message;
		std::string base_currency;
		std::string quote_currency;
		double base_increment;
		double quote_increment;
		double base_min_size;
		double base_max_size;
		double min_market_funds;
		double max_market_funds;
		bool cancel_only;
		bool limit_only;
		bool post_only;
		bool trading_disabled;

	private:
		template<typename> friend class Response;

		template<typename T>
		std::pair<int, std::string> fromJSON(const T& parser) {
			parser.get<std::string>("id", id);
			parser.get<std::string>("display_name", display_name);
			parser.get<std::string>("status", status);
			parser.get<std::string>("status_message", status_message);
			parser.get<std::string>("base_currency", base_currency);
			parser.get<std::string>("quote_currency", quote_currency);
			parser.get<double>("base_increment", base_increment);
			parser.get<double>("quote_increment", quote_increment);
            parser.get<double>("base_min_size", base_min_size);
            parser.get<double>("base_max_size", base_max_size);
            parser.get<double>("min_market_funds", min_market_funds);
            parser.get<double>("max_market_funds", max_market_funds);
			parser.get<bool>("cancel_only", cancel_only);
			parser.get<bool>("limit_only", limit_only);
			parser.get<bool>("post_only", post_only);
			parser.get<bool>("trading_disabled", trading_disabled);
			return std::make_pair(0, "OK");
		}
	};
} // namespace gdax