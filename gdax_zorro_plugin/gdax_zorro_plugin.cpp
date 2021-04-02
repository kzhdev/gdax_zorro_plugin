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
#include "gdax/websocket.h"

#define PLUGIN_VERSION	2

using namespace gdax;

namespace {
    TimeInForce s_tif = TimeInForce::FOK;
    std::string s_asset;
    int s_multiplier = 1;
    Logger* s_logger = nullptr;
    int s_priceType = 0;
    std::unique_ptr<GdaxWebsocket> wsClient;
    bool s_postOnly = true;
    std::string s_lastOrder;
    double s_limitPrice = 0.;
    double s_amount = 1;
    uint32_t s_residual = 0;
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

        wsClient = std::make_unique<GdaxWebsocket>();
        return;
    }

    DLLFUNC_C int BrokerLogin(char* User, char* Pwd, char* Type, char* Account)
    {
        if (!User) // log out
        {
            wsClient->logout();
            return 0;
        }

        // reset global variables
        s_residual = 0;
        s_priceType = 0;
        s_lastOrder = "";
        s_limitPrice = 0.;
        s_postOnly = true;
        s_amount = 1;
        s_tif = TimeInForce::FOK;

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

        if (!wsClient->login(apiKey, passphrase, secret, isPaperTrading)) {
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
        const auto* product = client->getProduct(Asset);
        if (!product) {
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
            *pLotAmount = product->base_min_size;
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
        long firstCandelTime = 0;
        do {
            auto response = client->getCandles(Asset, start, end, nTickMinutes * 60, nTicks);
            if (!response) {
                BrokerError(response.what().c_str());
                break;
            }

            if (response.content().candles.empty()) {
                break;
            }

            auto& candles = response.content().candles;
            s_logger->logDebug("%d candles downloaded\n", candles.size());
            for (auto& candle : candles) {
                __time32_t barCloseTime = candle.time + nTickMinutes * 60;
                if (barCloseTime > end) {
                    // end time cannot exceeds tEnd
                    continue;
                }

                auto& tick = ticks[barsDownloaded++];
                // change time to bar close time
                tick.time = convertTime(barCloseTime);
                tick.fOpen = (float)candle.open;
                tick.fHigh = (float)candle.high;
                tick.fLow = (float)candle.low;
                tick.fClose = (float)candle.close;
                tick.fVol = (float)candle.volume;

                if (barsDownloaded == nTicks) {
                    break;
                }
            }
            firstCandelTime = candles[candles.size() - 1].time;
            end = firstCandelTime - 30;
        } while (firstCandelTime > start && barsDownloaded < nTicks);
        s_logger->logDebug("%d candles returned\n", barsDownloaded);
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
        const auto* product = client->getProduct(Asset);
        if (!product) {
            BrokerError(("Invalid product " + std::string(Asset)).c_str());
            return 0;
        }

        OrderSide side = nAmount > 0 ? OrderSide::Buy : OrderSide::Sell;
        OrderType type = OrderType::Market;
        if (dLimit) {
            type = OrderType::Limit;
        }

        s_logger->logDebug("BrokerBuy2 %s nAmount=%d dStopDist=%f limit=%f\n", Asset, nAmount, dStopDist, dLimit);

        auto response = client->submitOrder(product, std::abs(nAmount) * product->base_min_size + s_residual * product->base_increment, side, type, s_tif, dLimit, dStopDist, s_postOnly);
        if (!response || !response.content()) {
            if (response.getCode() == -2) {
                return -2;
            }
            if (response.what() != "time in force") {
                BrokerError(response.what().c_str());
            }
            return 0;
        }

        auto& order = *response.content();
        s_lastOrder = order.id;

        if (order.filled_size) {
            if (pPrice) {
                *pPrice = order.filled_price;
            }
            if (pFill) {
                *pFill = int(order.filled_size / product->base_min_size);
            }
        }
        else {
            if (pFill) {
                *pFill = 0;
            }
        }
        return order.client_oid;
    }

    DLLFUNC_C int BrokerTrade(int nTradeID, double* pOpen, double* pClose, double* pCost, double *pProfit) {
        /*if (nTradeID != -1) {
            BrokerError(("nTradeID " + std::to_string(nTradeID) + " not valid. Need to be an UUID").c_str());
            return NAY;
        }*/
        
        Response<Order*> response = client->getOrder(nTradeID);
        if (!response) {
            BrokerError(response.what().c_str());
            return NAY;
        }

        auto* order = response.content();

        if (pOpen && order->filled_size) {
            *pOpen = order->filled_price;
        }

        if (pCost && order->filled_size) {
            *pCost = order->fill_fees;

            if (order->closeOrder) {
                *pClose += order->closeOrder->fill_fees;
            }
        }

        const auto* product = client->getProduct(order->product_id.c_str());
        return int(order->filled_size / product->base_min_size);
    }

    DLLFUNC_C int BrokerSell2(int nTradeID, int nAmount, double Limit, double* pClose, double* pCost, double* pProfit, int* pFill) {
        s_logger->logDebug("BrokerSell2 nTradeID=%d nAmount=%d limit=%f\n", nTradeID, nAmount, Limit);

        Response<Order*> response = client->getOrder(nTradeID);
        if (!response) {
            BrokerError(response.what().c_str());
            return 0;
        }

        auto* order = response.content();
        if (!nAmount) {
            // cancel order
            if (order->status == "canceled" || order->status == "done") {
                return nTradeID;
            }
            auto response = client->cancelOrder(*order);
            if (response) {
                return nTradeID;
            }
            BrokerError(response.what().c_str());
            return 0;
        }

        auto size = std::abs(nAmount) * s_amount;
        if (order->status == "done" || (order->filled_size && order->filled_size >= size)) {
            // order has been filled, close open position
            auto closeTradeId = BrokerBuy2((char*)order->product_id.c_str(), -nAmount, 0, Limit, pClose, pFill);
            if (closeTradeId > 0) {
                Order* closeOrder = nullptr;
                auto start = std::time(nullptr);
                do {
                    auto rsp = client->getOrder(closeTradeId);
                    if (rsp) {
                        auto* closeOrder = rsp.content();
                        if (closeOrder->status == "done") {
                            order->closeOrder = closeOrder;
                            if (pCost) {
                                *pCost = closeOrder->fill_fees + order->fill_fees;
                            }
                            if (pProfit) {
                                if (order->side == OrderSide::Buy) {
                                    *pProfit = closeOrder->executed_value - order->executed_value;
                                }
                                else {
                                    *pProfit = order->executed_value - closeOrder->executed_value;
                                }
                            }

                            if (order->filled_size == size) {
                                client->onPositionClosed(nTradeID);
                                client->onPositionClosed(closeTradeId);
                            }
                        }
                        return nTradeID;
                    }
                    BrokerProgress(1);
                }
                while (closeOrder && closeOrder->status != "done" && (std::time(nullptr) - start) < 10);
            }
            return 0;
        }
        else {
            BrokerError(("Close working order " + std::to_string(nTradeID)).c_str());
            if (order->filled_size) {
                auto closeTradeId = BrokerBuy2((char*)order->product_id.c_str(), order->side == OrderSide::Buy ? -order->filled_size / s_amount : order->filled_size / s_amount, 0, Limit, pClose, pFill);
                if (closeTradeId > 0) {
                    Order* closeOrder = nullptr;
                    auto start = std::time(nullptr);
                    do {
                        auto rsp = client->getOrder(closeTradeId);
                        if (rsp) {
                            closeOrder = rsp.content();
                            if (closeOrder->status == "done") {
                                order->closeOrder = closeOrder;
                                if (pCost) {
                                    *pCost = closeOrder->fill_fees + order->fill_fees;
                                }
                                if (pProfit) {
                                    if (order->side == OrderSide::Buy) {
                                        *pProfit = (closeOrder->executed_value - order->executed_value);
                                    }
                                    else {
                                        *pProfit = (order->executed_value - closeOrder->executed_value);
                                    }
                                }
                                client->onPositionClosed(closeOrder->client_oid);
                            }
                        }
                        BrokerProgress(1);
                    }                    
                    while (closeOrder && closeOrder->status != "done" && (std::time(nullptr) - start) < 10);
                }
            }
            auto diff = order->size - (size - order->filled_size);
            assert(diff >= 0);
            auto response = client->cancelOrder(*order);
            if (!response) {
                BrokerError(("Failed to close trade " + std::to_string(order->client_oid) + "(" + order->id + "). " + response.what()).c_str());
                return 0;
            }
            client->onPositionClosed(nTradeID);

            if (diff > 0) {
                return BrokerBuy2((char*)order->product_id.c_str(), order->side == OrderSide::Buy ? diff / s_amount : -diff / s_amount, 0, Limit, nullptr, nullptr);
            }
            return nTradeID;
        }
        return 0;
    }

    int32_t getPosition(const std::string& currency) {
        if (currency.find("-") != std::string::npos) {
            BrokerError("Invalid currenty. GET_POSITON command take a currency not an Asset");
            return 0;
        }
        auto response = client->getAccounts();
        if (!response) {
            for (auto& account : response.content()) {
                if (account.currency != currency) {
                    continue;
                }

                return account.available;
            }
            BrokerError(("Invalid currenty " + currency).c_str());
            return 0;
        }

        return 0;
    }

    constexpr int tifToZorroOrderType(TimeInForce tif) noexcept {
        constexpr const int converter[] = {2, 0, 0, 1};
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
                    prod.quote_increment, prod.base_min_size, prod.id.c_str());
            }
            else if (!isnan(ticker.ask)) {
                fprintf(f, "%s,%.8f,NAN,0.0,0.0,%.8f,%.8f,0.0,1,%.8f,0.000,%s\n",
                    prod.display_name.c_str(), ticker.ask, prod.quote_increment,
                    prod.quote_increment, prod.base_min_size, prod.id.c_str());
            }
            else {
                fprintf(f, "%s,NAN,NAN,0.0,0.0,%.8f,%.8f,0.0,1,%.8f,0.000,%s\n",
                    prod.display_name.c_str(), prod.quote_increment,
                    prod.quote_increment, prod.base_min_size, prod.id.c_str());
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
                std::string s(token);
                auto pos = s.find("/");
                if (pos != std::string::npos) {
                    s[pos] = '-';
                }
                auto it = products.find(s);
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

        case GET_UUID:
            strncpy((char*)dwParameter, s_lastOrder.c_str(), s_lastOrder.size() + 1);
            return dwParameter;

        case SET_UUID:
            s_lastOrder = (char*)dwParameter;
            return dwParameter;

        case SET_AMOUNT:
            s_amount = *(double*)dwParameter;
            break;

        case GET_MAXTICKS:
            return 300;

        case GET_MAXREQUESTS:
            // private api rate limit is 5/sec, public api rate limit is 3/sec
            // throttler will guard the request
            return 3;

        case GET_LOCK:
            return 1;

        case GET_POSITION:
            return getPosition((char*)dwParameter);

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
                s_tif = TimeInForce::FOK;
                break;
            case 2:
                s_tif = TimeInForce::GTC;
                break;
            case 3:
                s_tif = TimeInForce::IOC;
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

        case SET_LIMIT:
            s_limitPrice = *(double*)dwParameter;
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
        case SET_ORDERTEXT:
            break;

        case 2000: {
            s_postOnly = (int)dwParameter == 1;
            break;
        }

        case 2001: {
            downloadAssets((char*)dwParameter);
            break;
        }

        case 2002: {
            s_residual = (uint32_t)dwParameter;
            break;
        }

        default:
            s_logger->logDebug("Unhandled command: %d %lu\n", Command, dwParameter);
            break;
        }
        return 0;
    }
}
