#pragma once

#include <string>
#include <cstdint>

namespace gdax {

    struct Time {
        std::string iso;
        uint64_t epoch;

    private:
		template<typename> friend class Response;

		template<typename T>
		std::pair<int, std::string> fromJSON(const T& parser) {
			parser.get<std::string>("iso", iso);
			double epoch_time;
			parser.get<double>("epoch", epoch_time);
			epoch = epoch_time * 1000;
			return std::make_pair(0, "OK");
		}
    };
}