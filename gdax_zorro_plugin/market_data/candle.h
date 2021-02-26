#pragma once

#include <map>
#include <string>
#include <vector>

namespace gdax {
	struct Candle {
		uint32_t time;
		double low;
		double high;
		double open;
		double close;
		double volume;
	};

	struct Candles {
		std::vector<Candle> candles;

	private:
		template<typename> friend class Response;

		template<typename T>
		std::pair<int, std::string> fromJSON(const T& parser) {
			assert(parser.json.IsArray());
			auto candleArray = parser.json.GetArray();
			candles.resize(candleArray.Size());
			for (rapidjson::SizeType i = 0; i < candleArray.Size(); ++i) {
				auto& item = candleArray[i];
				if (item.IsArray()) {
					auto arr = item.GetArray();
					auto& candle = candles[i];
					for (rapidjson::SizeType j = 0; j < arr.Size(); ++j) {
						switch (j) {
						case 0:
							candle.time = arr[j].GetUint();
							break;
						case 1:
							candle.low = arr[j].GetDouble();
							break;
						case 2:
							candle.high = arr[j].GetDouble();
							break;
						case 3:
							candle.open = arr[j].GetDouble();
							break;
						case 4:
							candle.close = arr[j].GetDouble();
							break;
						case 5:
							candle.volume = arr[j].GetDouble();
							break;
						}
					}
				}
			}
			return std::make_pair(0, "OK");
		}
	};
} // namespace gdax
