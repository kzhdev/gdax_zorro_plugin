# GdaxZorroPlugin

**GdaxZorroPlugin** is a plugin for **[Zorro](https://zorro-project.com/)**, an institutional-grade development tool fro financial research and automatic traiding system. It provides connectivity to [Coinbase Pro](https://pro.coinbase.com/) cryptocurriency exchange.

## Install

To install the plugin, download the [latest release](https://github.com/kzhdev/gdax_zorro_plugin/releases/download/v0.0.1/Gdax_v0.0.1.zip) and place the Gdax.dll file into the **Plugin** folder under Zorro's root path.

## How to Use

* First generate a API Key in [Coinbase Pro](https://pro.coinbase.com/) website.
* In Zorro, select Gdax.
* Enter the **API Key_Passphrase** in the **User ID** input box
* Enter the **Secret Key** in the **Password** input box.

## Features

* Support **Limit**, **Market** order types

  ```C++
  // By default, enterLong/enterShort places a Market order
  enterLong(5);   // long 5 lot at Market price
  enterShort(5);  // short 5 lot at Market price

  // Place a limit
  OrderLimit = 100.00;  // set a Limit price first
  enterLong(5);   // place 5 lot at 100.00
  ```

* Support **FOK**, **IOC**, **GTC**, TimeInForce types.

  ```C++
  // By default, limit order has FOK TimeInfoForce type
  // Use borkerCommand(SET_ORDERTYPE, tif) to change TimeInForce.
  // Valid TimeInForce value is
  //  1 - FOK
  //  2 - GTC
  //  3 - IOC
  //
  // NOTE: brokemand(SET_ORDERTYPE, 0) will be ignored, this is because Zorro always call brokerCommand(SET_ORDERTYPE, 0) before setting limit price.

  brokerCommand(SET_ORDERTYPE, 2);  // set TIF to GTC
  OrderLimit = 100.00;
  enterShort(5);    // Sell 5 lot GTC order at limit price 100.00
  ```

* Support LastQuote/LastTrade price type

  ```C++
  // By default, it use ask/bid price mode
  // Use brokerCommand(SET_PRICETYPE, 2) to change price type to trades
  brokerCommand(SET_PRICETYPE, 2) // Set price type to trades
  brokerCommand(SET_PRICETYPE, 1 /*or 0*/) // Set price type to ask/bid quote
  ```

* Support Position(Balance) retrieval

  ```C++
  // get balance for specific currency
  brokerCommand(GET_POSITION, "BTC");
  ```

* Set PostOnly limit order flag through custom brokerCommand

  ``` C++
  // set PostOnly to true
  brokerCommand(2000, true);
  ```

* Generate AssetList file through custom borkerCommand
  
  ``` C++
  brokerCommand(2001, char *symbols);
  ```

  **symbols** - One or more symbols separated by comma. If symbols = **0**, all symbols will be included.
  An AssetCoinbasePro.csv file will be generated in the Log diredtory.

  ``` C++
  Exemple:
  // GenerateAlpacaAssetList.c
  function main() {
    brokerCommand(2001, "BTC-USD,ETH-USD");  // Generate AssetsAlpaca.csv contains BTC-USD, ETH-USD symbols
  }
  ```

* 1 lot equals the base_min_size of the product. Use brokerCommand 2002 to add additional frantion of lot

  ```C++
  // 1 Lot equals the base_min_size of the product
  // For example, for BTC-USD, 1 lot = 0.0001 BTC
  // The base_min_increment of BTC-USD is 0.00000001
  // Use brokerCommand(2002, residual) to add additional fraction of lot to the Lots.
  // For example:
  asset("BTC/USD");
  Lots = 10;                    // order size is Lots * base_min_size, 10 * 0.0001
  brokerCommand(2002, 5555);    // This command add additional size of 5555 * base_min_increment to the order size;
  enterLong();
  // This buys 0.00105555 BTC at market price
  ```

* Following Zorro Broker API functions has been implemented:

  * BrokerOpen
  * BrokerHTTP
  * BrokerLogin
  * BrokerTime
  * BrokerAsset
  * BrokerHistory2
  * BrokerBuy2
  * BrokerTrade
  * BrokerSell2
  * BrokerCommand
    * GET_COMPLIANCE
    * GET_MAXTICKS
    * GET_MAXREQUESTS
    * GET_LOCK
    * GET_POSITION
    * GET_PRICETYPE
    * GET_UUID
    * SET_SYMBOL
    * SET_ORDERTYPE
    * SET_PRICETYPE
    * SET_DIAGNOSTICS
    * SET_UUID

## [Build From Source](BUILD.md)

## Bug Report

If you find any issue or have any suggestion, please report in GitHub [issues](https://github.com/kzhdev/gdax_zorro_plugin/issues).
