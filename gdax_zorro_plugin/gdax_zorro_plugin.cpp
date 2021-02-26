// GdaxZorroPlugin.cpp : Defines the exported functions for the DLL application.
//
// Coinbase Pro plugin for Zorro Automated Trading System
// Written by Kun Zhao
//

#include "stdafx.h"

#include "gdax_zorro_plugin.h"
#include "include\trading.h"	// enter your path to trading.h (in your Zorro folder)

// standard library
#include <string>
#include <sstream>
#include <vector>
#include <memory>

#include "gdax/client.h"
#include "logger.h"
#include "include/functions.h"

#define PLUGIN_VERSION	2

using namespace gdax;

namespace {
    TimeInForce s_tif = TimeInForce::FOK;
    std::string s_asset;
    int s_multiplier = 1;
    Logger* s_logger = nullptr;
    std::string s_nextOrderText;
    int s_priceType = 0;
    std::unordered_map<uint32_t, Order> s_mapOrderByClientOrderId;
}

namespace gdax
{
    std::unique_ptr<Client> client = nullptr;

    ////////////////////////////////////////////////////////////////
    DLLFUNC_C int BrokerOpen(char* Name, FARPROC fpError, FARPROC fpProgress)
    {
        strcpy_s(Name, 32, "Coinbase Pro");
        (FARPROC&)BrokerError = fpError;
        (FARPROC&)BrokerProgress = fpProgress;
        return PLUGIN_VERSION;
    }

    DLLFUNC_C void BrokerHTTP(FARPROC fpSend, FARPROC fpStatus, FARPROC fpResult, FARPROC fpFree)
    {
        (FARPROC&)http_send = fpSend;
        (FARPROC&)http_status = fpStatus;
        (FARPROC&)http_result = fpResult;
        (FARPROC&)http_free = fpFree;
        return;
    }

    DLLFUNC_C int BrokerLogin(char* User, char* Pwd, char* Type, char* Account)
    {
        if (!User) // log out
        {
            return 0;
        }

        bool isPaperTrading = strcmp(Type, "Demo") == 0;

        std::string key_phrase(User);
        std::string apiKey(key_phrase.substr(0, 32));
        std::string passphrase(key_phrase.substr(33));
        std::string secret(Pwd);
        try {
            client = std::make_unique<Client>(apiKey, passphrase, secret, isPaperTrading);
        }
        catch (const std::runtime_error&) {
            return 0;
        }
        
        s_logger = &client->logger();

        //attempt login
        auto response = client->getAccounts();
        if (!response) {
            BrokerError("Login failed.");
            BrokerError(response.what().c_str());
            return 0;
        }

        auto& accounts = response.content();
        if (!accounts.empty()) {
            BrokerError(("Account " + accounts[0].profile_id).c_str());
            sprintf_s(Account, 1024, accounts[0].profile_id.c_str());
        }
        return 1;
    }

    DATE convertTime(__time32_t t32)
    {
        return (DATE)t32 / (24. * 60. * 60.) + 25569.; // 25569. = DATE(1.1.1970 00:00)
    }

    __time32_t convertTime(DATE date)
    {
        return (__time32_t)((date - 25569.) * 24. * 60. * 60.);
    }

    DLLFUNC_C int BrokerTime(DATE* pTimeGMT) {
        auto rspTime = client->getTime();
        if (rspTime) {
            *pTimeGMT = convertTime(__time32_t(rspTime.content().epoch / 1000));
        }
        return 2; 
    }

    DLLFUNC_C int BrokerAsset(char* Asset, double* pPrice, double* pSpread, double* pVolume, double* pPip, double* pPipCost, double* pLotAmount, double* pMarginCost, double* pRollLong, double* pRollShort)
    {
        const auto& products = client->getProducts();
        auto iter = products.find(Asset);
        if (iter == products.end()) {
            BrokerError(("Asset " + std::string(Asset) + " not found").c_str());
            return 0;
        }

        if (!pPrice) {
            // this is subscribe
            return 1;
        }

        auto response = client->getTicker(Asset);
        if (!response) {
            BrokerError(("Failed to get ticker " + std::string(Asset) + " error: " + response.what()).c_str());
            return 0;
        }

        if (s_priceType == 2) {
            if (pPrice) {
                *pPrice = response.content().price;
            }

            if (pSpread) {
                *pSpread = 0.0;
            }
        }
        else {
            if (pPrice) {
                *pPrice = response.content().ask;
            }

            if (pSpread) {
                *pSpread = response.content().ask - response.content().bid;
            }
        }

        if (pLotAmount) {
            *pLotAmount = iter->second.base_increment;
        }

        if (pRollLong) {
            *pRollLong = 0.;
        }

        if (pRollShort) {
            *pRollShort = 0.;
        }
        return 1;
    }

    DLLFUNC_C int BrokerHistory2(char* Asset, DATE tStart, DATE tEnd, int nTickMinutes, int nTicks, T6* ticks)
    {
        if (!client || !Asset || !ticks || !nTicks) return 0;

        if (!nTickMinutes) {
            assert(0);
            BrokerError("Tick data download is not supported by Alpaca.");
            return 0;
        }

        auto start = convertTime(tStart);
        auto end = convertTime(tEnd);

        s_logger->logDebug("BorkerHisotry %s start: %d end: %d nTickMinutes: %d nTicks: %d\n", Asset, start, end, nTickMinutes, nTicks);

        int barsDownloaded = 0;
        uint32_t firstCandelTime = 0;
        do {
            auto response = client->getCandles(Asset, start, end, nTickMinutes * 60, nTicks);
            if (!response) {
                BrokerError(response.what().c_str());
                return barsDownloaded;
            }

            auto& candles = response.content();
            s_logger->logDebug("%d candles downloaded\n", candles.candles.size());
            for (auto& candle : candles.candles) {
                __time32_t barCloseTime = candle.time + nTickMinutes * 60;
                if (barCloseTime > end) {
                    // end time cannot exceeds tEnd
                    continue;
                }

                auto& tick = ticks[barsDownloaded++];
                // change time to bar close time
                tick.time = convertTime(barCloseTime);
                tick.fOpen = candle.open;
                tick.fHigh = candle.high;
                tick.fLow = candle.low;
                tick.fClose = candle.close;
                tick.fVol = candle.volume;

                if (barsDownloaded == nTicks) {
                    break;
                }
            }
            firstCandelTime = candles.candles[candles.candles.size() - 1].time;
            end = firstCandelTime - 30;
        } while (firstCandelTime > start && barsDownloaded < nTicks);
        return barsDownloaded;
    }

    DLLFUNC_C int BrokerAccount(char* Account, double* pdBalance, double* pdTradeVal, double* pdMarginVal)
    {
        auto response = client->getAccounts();
        if (!response) {
            return 0;
        }

        for (auto& account : response.content()) {
            if (account.currency == "USD") {
                if (pdBalance) {
                    *pdBalance = account.balance;
                }

                if (pdTradeVal) {
                    *pdTradeVal = account.balance - account.available;
                }
                break;
            }
        }
        return 1;
    }

    DLLFUNC_C int BrokerBuy2(char* Asset, int nAmount, double dStopDist, double dLimit, double* pPrice, int* pFill) 
    {
        auto start = std::time(nullptr);

        OrderSide side = nAmount > 0 ? OrderSide::Buy : OrderSide::Sell;
        OrderType type = OrderType::Market;
        if (dLimit) {
            type = OrderType::Limit;
        }
        std::string limit;
        if (dLimit) {
            limit = std::to_string(dLimit);
        }
        std::string stop;
        if (dStopDist) {

        }

        s_logger->logDebug("BrokerBuy2 %s orderText=%s nAmount=%d dStopDist=%f limit=%f\n", Asset, s_nextOrderText.c_str(), nAmount, dStopDist, dLimit);

        auto response = client->submitOrder(Asset, std::abs(nAmount), side, type, s_tif, limit, stop, false, s_nextOrderText);
        if (!response) {
            BrokerError(response.what().c_str());
            return 0;
        }

        auto* order = &response.content();
        auto exchOrdId = order->id;
        auto internalOrdId = order->internal_id;
        s_mapOrderByClientOrderId.emplace(internalOrdId, *order);

        if (order->filled_qty) {
            if (pPrice) {
                *pPrice = response.content().filled_avg_price;
            }
            if (pFill) {
                *pFill = response.content().filled_qty;
            }
            //return -1;
            return internalOrdId;
        }

        if (s_tif == TimeInForce::IOC || s_tif == TimeInForce::FOK) {
            // order not filled in the submitOrder response
            // query order status to get fill status
            do {
                auto response2 = client->getOrder(exchOrdId, false, true);
                if (!response2) {
                    break;
                }
                order = &response2.content();
                s_mapOrderByClientOrderId[internalOrdId] = *order;
                if (pPrice) {
                    *pPrice = order->filled_avg_price;
                }
                if (pFill) {
                    *pFill = order->filled_qty;
                }

                if (order->status == "canceled" ||
                    order->status == "filled" ||
                    order->status == "expired") {
                    break;
                }

                auto timePast = std::difftime(std::time(nullptr), start);
                if (timePast >= 30) {
                    auto response3 = client->cancelOrder(exchOrdId);
                    if (!response3) {
                        BrokerError(("Failed to cancel unfilled FOK/IOC order " + exchOrdId + " " + response3.what()).c_str());
                    }
                    return 0;
                }
            } while (!order->filled_qty);
        }
        //return -1;
        return internalOrdId;
    }

    DLLFUNC_C int BrokerTrade(int nTradeID, double* pOpen, double* pClose, double* pCost, double *pProfit) {
        s_logger->logInfo("BrokerTrade: %d\n", nTradeID);
       /* if (nTradeID != -1) {
            BrokerError(("nTradeID " + std::to_string(nTradeID) + " not valid. Need to be an UUID").c_str());
            return NAY;
        }*/
        
        Response<Order> response;
        auto iter = s_mapOrderByClientOrderId.find(nTradeID);
        if (iter == s_mapOrderByClientOrderId.end()) {
            // unknown order?
            std::stringstream clientOrderId;
            clientOrderId << "ZORRO_";
            if (!s_nextOrderText.empty()) {
                clientOrderId << s_nextOrderText << "_";
            }
            clientOrderId << nTradeID;
            response = client->getOrderByClientOrderId(clientOrderId.str());
            if (!response) {
                BrokerError(response.what().c_str());
                return NAY;
            }
            s_mapOrderByClientOrderId.insert(std::make_pair(nTradeID, response.content()));
        }
        else {
            response = client->getOrder(iter->second.id);
            if (!response) {
                BrokerError(response.what().c_str());
                return NAY;
            }
        }

        auto& order = response.content();
        s_mapOrderByClientOrderId[nTradeID] = order;

        if (pOpen) {
            *pOpen = order.filled_avg_price;
        }

        if (pProfit && order.filled_qty) {
            //auto resp = pMarketData->getLastQuote(order.symbol);
            //if (resp) {
            //    auto& quote = resp.content().quote;
            //    *pProfit = order.side == OrderSide::Buy ? ((quote.ask_price - order.filled_avg_price) * order.filled_qty) : (order.filled_avg_price - quote.bid_price) * order.filled_qty;
            //}
        }
        return order.filled_qty;
    }

    DLLFUNC_C int BrokerSell2(int nTradeID, int nAmount, double Limit, double* pClose, double* pCost, double* pProfit, int* pFill) {
        s_logger->logDebug("BrokerSell2 nTradeID=%d nAmount=%d limit=%f\n", nTradeID,nAmount, Limit);

        auto iter = s_mapOrderByClientOrderId.find(nTradeID);
        if (iter == s_mapOrderByClientOrderId.end()) {
            BrokerError(("Order " + std::to_string(nTradeID) + " not found.").c_str());
            return 0;
        }

        auto& order = iter->second;
        if (order.status == "filled") {
            // order has been filled
            auto closeTradeId = BrokerBuy2((char*)order.symbol.c_str(), -nAmount, 0, Limit, pProfit, pFill);
            if (closeTradeId) {
                auto iter2 = s_mapOrderByClientOrderId.find(closeTradeId);
                if (iter2 != s_mapOrderByClientOrderId.end()) {
                    auto& closeTrade = iter2->second;
                    if (pClose) {
                        *pClose = closeTrade.filled_avg_price;
                    }
                    if (pFill) {
                        *pFill = closeTrade.filled_qty;
                    }
                    if (pProfit) {
                        *pProfit = (closeTrade.filled_avg_price - order.filled_avg_price) * closeTrade.filled_qty;
                    }
                }
                return nTradeID;
            }
            return 0;
        }
        else {
            // close working order?
            BrokerError(("Close working order " + std::to_string(nTradeID)).c_str());
            if (std::abs(nAmount) == order.qty) {
                auto response = client->cancelOrder(iter->second.id);
                if (response) {
                    return nTradeID;
                }
                BrokerError(("Failed to close trade " + std::to_string(nTradeID) + " " + response.what()).c_str());
                return 0;
            }
            else {
                auto response = client->replaceOrder(order.id, iter->second.qty - nAmount, order.tif, (Limit ? std::to_string(Limit) : ""), "", iter->second.client_order_id);
                if (response) {
                    auto& replacedOrder = response.content();
                    uint32_t orderId = replacedOrder.internal_id;
                    s_mapOrderByClientOrderId.emplace(orderId, std::move(replacedOrder));
                    return orderId;
                }
                BrokerError(("Failed to modify trade " + std::to_string(nTradeID) + " " + response.what()).c_str());
                return 0;
            }
        }
    }

    int32_t getPosition(const std::string& asset) {
        auto response = client->getPosition(asset);
        if (!response) {
            if (response.getCode() == 40410000) {
                // no open position
                return 0;
            }

            BrokerError(("Get position failed. " + response.what()).c_str());
            return 0;
        }

        return response.content().qty;
    }

    constexpr int tifToZorroOrderType(TimeInForce tif) noexcept {
        constexpr const int converter[] = {0, 2, 0, 0, 1, 0};
        assert(tif >= 0 && tif < sizeof(converter) / sizeof(int));
        return converter[tif];
    }

    void downloadAssets(char* symbols) {
        FILE* f;
        if (fopen_s(&f, "./Log/AssetsCoinbasePro.csv", "w+")) {
            s_logger->logError("Failed to open ./Log/AssetsCoinbasePro.csv file\n");
            return;
        }

        BrokerError("Generating Asset List...");
        fprintf(f, "Name,Price,Spread,RollLong,RollShort,PIP,PIPCost,MarginCost,Leverage,LotAmount,Commission,Symbol\n");

        auto writeProduct = [f](const Product& prod) {
            BrokerError(("Asset " + prod.display_name).c_str());
            BrokerProgress(1);
            auto rspTiker = client->getTicker(prod.id);
            if (!rspTiker) {
              BrokerError(rspTiker.what().c_str());
              return;
            }
            
            auto& ticker = rspTiker.content();
            if (!isnan(ticker.ask) && !isnan(ticker.bid)) {
                fprintf(f, "%s,%.8f,%.8f,0.0,0.0,%.8f,%.8f,0.0,1,%.8f,0.000,%s\n",
                    prod.display_name.c_str(), ticker.ask, (ticker.ask - ticker.bid), prod.quote_increment,
                    prod.quote_increment, prod.base_increment, prod.id.c_str());
            }
            else if (!isnan(ticker.ask)) {
                fprintf(f, "%s,%.8f,NAN,0.0,0.0,%.8f,%.8f,0.0,1,%.8f,0.000,%s\n",
                    prod.display_name.c_str(), ticker.ask, prod.quote_increment,
                    prod.quote_increment, prod.base_increment, prod.id.c_str());
            }
            else {
                fprintf(f, "%s,NAN,NAN,0.0,0.0,%.8f,%.8f,0.0,1,%.8f,0.000,%s\n",
                    prod.display_name.c_str(), prod.quote_increment,
                    prod.quote_increment, prod.base_increment, prod.id.c_str());
            }
        };

        if (!symbols) {
            for (auto& kvp : client->getProducts()) {
                writeProduct(kvp.second);
            }
        }
        else {
            const char* delim = ",";
            char* next_token;
            char* token = strtok_s(symbols, delim, &next_token);
            const auto& products = client->getProducts();
            while (token != nullptr) {
                auto it = products.find(token);
                if (it != products.end()) {
                    writeProduct(it->second);
                }
                else {
                    BrokerError((std::string(token) + " not found").c_str());
                }
                token = strtok_s(nullptr, delim, &next_token);
            }
        }
        
        fflush(f);
        fclose(f);
        s_logger->logDebug("close file\n");
    }
    
    DLLFUNC_C double BrokerCommand(int Command, DWORD dwParameter)
    {
        static int SetMultiplier;
        std::string Data, response;
        int i = 0;
        double price = 0., spread = 0.;

        switch (Command)
        {
        case GET_COMPLIANCE:
            return 15; // full NFA compliant

        //case GET_BROKERZONE:
            //return ET; // for now since Alpaca only support US

        case GET_MAXTICKS:
            return 300;

        case GET_MAXREQUESTS:
            // private api rate limit is 5/sec, private api rate limit is 3/sec
            // throttler will guard the request
            return 5;

        case GET_LOCK:
            return -1;

        case GET_POSITION:
            return getPosition((char*)dwParameter);

        case SET_ORDERTEXT:
            s_nextOrderText = (char*)dwParameter;
            client->logger().logDebug("SET_ORDERTEXT: %s\n", s_nextOrderText.c_str());
            return dwParameter;

        case SET_SYMBOL:
            s_asset = (char*)dwParameter;
            return 1;

        case SET_MULTIPLIER:
            s_multiplier = (int)dwParameter;
            return 1;

        case SET_ORDERTYPE: {
           
            switch ((int)dwParameter) {
            case 0:
                return 0;
            case 1:
                s_tif = TimeInForce::IOC;
                break;
            case 2:
                s_tif = TimeInForce::GTC;
                break;
            case 3:
                s_tif = TimeInForce::FOK;
                break;
            case 4:
                s_tif = TimeInForce::Day;
                break;
            case 5:
                s_tif = TimeInForce::OPG;
                break;
            case 6:
                s_tif = TimeInForce::CLS;
                break;
            }

            if ((int)dwParameter >= 8) {
                return (int)dwParameter;
            }

            s_logger->logDebug("SET_ORDERTYPE: %d s_tif=%s\n", (int)dwParameter, to_string(s_tif));
            return tifToZorroOrderType(s_tif);
        }

        case GET_PRICETYPE:
            return s_priceType;

        case SET_PRICETYPE:
            s_priceType = (int)dwParameter;
            s_logger->logDebug("SET_PRICETYPE: %d\n", s_priceType);
            return dwParameter;

        case GET_VOLTYPE:
          return 0;

        case SET_DIAGNOSTICS:
            if ((int)dwParameter == 1 || (int)dwParameter == 0) {
                client->logger().setLevel((int)dwParameter ? LogLevel::L_DEBUG : LogLevel::L_OFF);
                return dwParameter;
            }
            break;

        case GET_BROKERZONE:
        case SET_HWND:
        case GET_CALLBACK:
            break;

        case 2001: {
            downloadAssets((char*)dwParameter);
            break;
        }
            

        default:
            s_logger->logDebug("Unhandled command: %d %lu\n", Command, dwParameter);
            break;
        }
        return 0;
    }
}
