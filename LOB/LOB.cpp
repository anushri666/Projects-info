/*
 * High-Frequency Limit Order Book (LOB) Simulator
 * Author: Anushka Shrivastava
 * Description: Discrete-event simulator with OBI, micro-price signals,
 *              and real-time bottleneck/spread analytics
 */

#include <iostream>
#include <map>
#include <unordered_map>
#include <list>
#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <iomanip>
#include <chrono>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>

// -----------------------------------------------------------------------
// 1. ORDER STRUCTURES
// -----------------------------------------------------------------------

enum class OrderType  { LIMIT, MARKET, CANCEL };
enum class OrderSide  { BUY, SELL };

struct Order {
    int         id;
    OrderType   type;
    OrderSide   side;
    double      price;
    int         quantity;
    long long   timestamp;  // microseconds

    Order(int id, OrderType t, OrderSide s, double p, int q, long long ts)
        : id(id), type(t), side(s), price(p), quantity(q), timestamp(ts) {}
};

// -----------------------------------------------------------------------
// 2. PRICE LEVEL: doubly-linked list of orders at one price
// -----------------------------------------------------------------------

struct PriceLevel {
    double              price;
    int                 totalQty;
    std::list<Order>    orders;   // FIFO queue per price

    PriceLevel() : price(0.0), totalQty(0) {}
    explicit PriceLevel(double p) : price(p), totalQty(0) {}

    void addOrder(const Order& o) {
        orders.push_back(o);
        totalQty += o.quantity;
    }

    // Returns filled qty
    int matchAgainst(int demandQty, std::vector<Order>& fills) {
        int filled = 0;
        while (!orders.empty() && filled < demandQty) {
            auto& front = orders.front();
            int take = std::min(front.quantity, demandQty - filled);
            filled        += take;
            totalQty      -= take;
            front.quantity -= take;
            fills.push_back(front);
            fills.back().quantity = take;
            if (front.quantity == 0) orders.pop_front();
        }
        return filled;
    }

    bool empty() const { return orders.empty(); }
};

// -----------------------------------------------------------------------
// 3. LIMIT ORDER BOOK
// -----------------------------------------------------------------------

class LimitOrderBook {
public:
    // Bids: highest price first → std::map with reverse comparator
    std::map<double, PriceLevel, std::greater<double>> bids;
    // Asks: lowest price first
    std::map<double, PriceLevel>                        asks;

    std::unordered_map<int, double> orderPriceMap; // id → price (for cancel)
    std::unordered_map<int, OrderSide> orderSideMap;

    int    tradeCount   = 0;
    double totalVolume  = 0.0;
    int    cancelCount  = 0;
    int    orderCount   = 0;

    // Spread & micro-price history
    std::vector<double> spreadHistory;
    std::vector<double> microPriceHistory;
    std::vector<double> obiHistory;

    // ---- Add limit order ----
    void addLimitOrder(Order o) {
        ++orderCount;
        if (o.side == OrderSide::BUY) {
            // Try to match against asks
            int remaining = o.quantity;
            while (remaining > 0 && !asks.empty()) {
                auto it = asks.begin();
                if (it->first > o.price) break;  // no cross
                std::vector<Order> fills;
                int filled = it->second.matchAgainst(remaining, fills);
                remaining   -= filled;
                totalVolume += filled;
                ++tradeCount;
                if (it->second.empty()) asks.erase(it);
            }
            if (remaining > 0) {
                o.quantity = remaining;
                if (bids.find(o.price) == bids.end())
                    bids[o.price] = PriceLevel(o.price);
                bids[o.price].addOrder(o);
                orderPriceMap[o.id] = o.price;
                orderSideMap[o.id]  = OrderSide::BUY;
            }
        } else {
            // Try to match against bids
            int remaining = o.quantity;
            while (remaining > 0 && !bids.empty()) {
                auto it = bids.begin();
                if (it->first < o.price) break;
                std::vector<Order> fills;
                int filled = it->second.matchAgainst(remaining, fills);
                remaining   -= filled;
                totalVolume += filled;
                ++tradeCount;
                if (it->second.empty()) bids.erase(it);
            }
            if (remaining > 0) {
                o.quantity = remaining;
                if (asks.find(o.price) == asks.end())
                    asks[o.price] = PriceLevel(o.price);
                asks[o.price].addOrder(o);
                orderPriceMap[o.id] = o.price;
                orderSideMap[o.id]  = OrderSide::SELL;
            }
        }
        recordMetrics();
    }

    // ---- Market order ----
    void addMarketOrder(Order o) {
        ++orderCount;
        int remaining = o.quantity;
        if (o.side == OrderSide::BUY) {
            while (remaining > 0 && !asks.empty()) {
                auto it = asks.begin();
                std::vector<Order> fills;
                int filled = it->second.matchAgainst(remaining, fills);
                remaining   -= filled;
                totalVolume += filled;
                ++tradeCount;
                if (it->second.empty()) asks.erase(it);
            }
        } else {
            while (remaining > 0 && !bids.empty()) {
                auto it = bids.begin();
                std::vector<Order> fills;
                int filled = it->second.matchAgainst(remaining, fills);
                remaining   -= filled;
                totalVolume += filled;
                ++tradeCount;
                if (it->second.empty()) bids.erase(it);
            }
        }
        recordMetrics();
    }

    // ---- Cancel order ----
    bool cancelOrder(int orderId) {
        auto pit = orderPriceMap.find(orderId);
        if (pit == orderPriceMap.end()) return false;
        double price = pit->second;
        OrderSide side = orderSideMap[orderId];

        auto removeFrom = [&](auto& book) {
            auto lit = book.find(price);
            if (lit == book.end()) return;
            auto& lvl = lit->second;
            for (auto it = lvl.orders.begin(); it != lvl.orders.end(); ++it) {
                if (it->id == orderId) {
                    lvl.totalQty -= it->quantity;
                    lvl.orders.erase(it);
                    ++cancelCount;
                    break;
                }
            }
            if (lvl.empty()) book.erase(lit);
        };

        if (side == OrderSide::BUY)  removeFrom(bids);
        else                          removeFrom(asks);

        orderPriceMap.erase(pit);
        orderSideMap.erase(orderId);
        return true;
    }

    // ---- Signals ----
    double bestBid() const {
        return bids.empty() ? 0.0 : bids.begin()->first;
    }
    double bestAsk() const {
        return asks.empty() ? 0.0 : asks.begin()->first;
    }
    double spread() const {
        if (bids.empty() || asks.empty()) return 0.0;
        return bestAsk() - bestBid();
    }

    // Micro-price: volume-weighted mid
    double microPrice() const {
        if (bids.empty() || asks.empty()) return 0.0;
        double Vb = bids.begin()->second.totalQty;
        double Va = asks.begin()->second.totalQty;
        double Pb = bestBid(), Pa = bestAsk();
        return (Pb * Va + Pa * Vb) / (Vb + Va);
    }

    // Order Book Imbalance [-1, +1]
    double OBI(int levels = 5) const {
        double bidVol = 0, askVol = 0;
        int cnt = 0;
        for (auto& [p, lvl] : bids) {
            if (cnt++ >= levels) break;
            bidVol += lvl.totalQty;
        }
        cnt = 0;
        for (auto& [p, lvl] : asks) {
            if (cnt++ >= levels) break;
            askVol += lvl.totalQty;
        }
        if (bidVol + askVol == 0) return 0.0;
        return (bidVol - askVol) / (bidVol + askVol);
    }

    void recordMetrics() {
        spreadHistory.push_back(spread());
        microPriceHistory.push_back(microPrice());
        obiHistory.push_back(OBI());
    }

    void printSnapshot(int topN = 5) const {
        std::cout << "\n=== ORDER BOOK SNAPSHOT (Top " << topN << ") ===\n";
        std::cout << std::left << std::setw(12) << "ASK Price"
                  << std::setw(10) << "Qty" << "\n";
        std::cout << std::string(22, '-') << "\n";

        // Print asks in reverse (highest first for display)
        std::vector<std::pair<double,int>> askLevels;
        int cnt = 0;
        for (auto& [p, lvl] : asks) {
            if (cnt++ >= topN) break;
            askLevels.push_back({p, lvl.totalQty});
        }
        for (auto it = askLevels.rbegin(); it != askLevels.rend(); ++it)
            std::cout << std::setw(12) << it->first << std::setw(10) << it->second << "\n";

        std::cout << "--- SPREAD: " << std::fixed << std::setprecision(4)
                  << spread() << " ---\n";

        cnt = 0;
        for (auto& [p, lvl] : bids) {
            if (cnt++ >= topN) break;
            std::cout << std::setw(12) << p << std::setw(10) << lvl.totalQty << "\n";
        }
        std::cout << std::left << std::setw(12) << "BID Price"
                  << std::setw(10) << "Qty" << "\n";
    }
};

// -----------------------------------------------------------------------
// 4. SYNTHETIC TICK DATA GENERATOR
// -----------------------------------------------------------------------

class TickGenerator {
    std::mt19937 rng;
    double midPrice;
    double tickSize;
    int    nextId;

public:
    TickGenerator(double mid = 100.0, double tick = 0.05)
        : rng(std::random_device{}()), midPrice(mid),
          tickSize(tick), nextId(1) {}

    Order generate(long long timestamp) {
        std::uniform_int_distribution<int> typeDist(0, 9);
        std::normal_distribution<double>   priceDist(midPrice, 0.5);
        std::uniform_int_distribution<int> qtyDist(1, 200);
        std::uniform_int_distribution<int> sideDist(0, 1);

        int typeRoll = typeDist(rng);
        OrderType type;
        if      (typeRoll < 6) type = OrderType::LIMIT;
        else if (typeRoll < 8) type = OrderType::MARKET;
        else                   type = OrderType::CANCEL;

        OrderSide side = (sideDist(rng) == 0) ? OrderSide::BUY : OrderSide::SELL;

        // Round price to tick size
        double rawPrice = priceDist(rng);
        double price    = std::round(rawPrice / tickSize) * tickSize;
        price           = std::max(price, midPrice - 2.0);
        price           = std::min(price, midPrice + 2.0);

        // Slowly drift mid price
        std::normal_distribution<double> drift(0.0, 0.01);
        midPrice += drift(rng);

        return Order(nextId++, type, side, price, qtyDist(rng), timestamp);
    }

    int peekNextId() const { return nextId; }
};

// -----------------------------------------------------------------------
// 5. SIMULATOR ENGINE
// -----------------------------------------------------------------------

class LOBSimulator {
    LimitOrderBook lob;
    TickGenerator  gen;

    std::vector<int>    cancelQueue;  // ids eligible for cancel
    int                 eventCount = 0;

public:
    LOBSimulator(double midPrice = 100.0) : gen(midPrice) {}

    void run(int numEvents = 10000) {
        auto wallStart = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < numEvents; ++i) {
            long long ts = i * 100; // 100 microsecond intervals
            Order o = gen.generate(ts);

            if (o.type == OrderType::CANCEL) {
                if (!cancelQueue.empty()) {
                    int idx = std::rand() % cancelQueue.size();
                    lob.cancelOrder(cancelQueue[idx]);
                    cancelQueue.erase(cancelQueue.begin() + idx);
                }
            } else if (o.type == OrderType::MARKET) {
                lob.addMarketOrder(o);
            } else {
                lob.addLimitOrder(o);
                cancelQueue.push_back(o.id);
            }
            ++eventCount;

            // Print snapshot every 2000 events
            if (i % 2000 == 0 && i > 0) {
                lob.printSnapshot();
                std::cout << "  OBI       : " << std::fixed << std::setprecision(4)
                          << lob.OBI() << "\n";
                std::cout << "  Micro-Price: " << lob.microPrice() << "\n";
                std::cout << "  Spread     : " << lob.spread() << "\n";
            }
        }

        auto wallEnd  = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(wallEnd - wallStart).count();

        printFinalStats(elapsed);
    }

    void printFinalStats(double elapsed) {
        // Compute average metrics
        auto avg = [](const std::vector<double>& v) {
            if (v.empty()) return 0.0;
            double s = 0; for (double x : v) s += x; return s / v.size();
        };
        auto maxVal = [](const std::vector<double>& v) {
            return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end());
        };
        auto minVal = [](const std::vector<double>& v) {
            return v.empty() ? 0.0 : *std::min_element(v.begin(), v.end());
        };

        std::cout << "\n========================================\n";
        std::cout << "        LOB SIMULATION SUMMARY\n";
        std::cout << "========================================\n";
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "Total Events Processed : " << eventCount          << "\n";
        std::cout << "Total Orders           : " << lob.orderCount      << "\n";
        std::cout << "Total Trades           : " << lob.tradeCount      << "\n";
        std::cout << "Total Cancellations    : " << lob.cancelCount     << "\n";
        std::cout << "Total Volume Traded    : " << lob.totalVolume     << "\n";
        std::cout << "Wall-Clock Runtime     : " << elapsed << "s\n";
        std::cout << "Events/second          : "
                  << (int)(eventCount / elapsed)                        << "\n";
        std::cout << "\n--- SPREAD ANALYTICS ---\n";
        std::cout << "Avg Spread  : " << avg(lob.spreadHistory)        << "\n";
        std::cout << "Max Spread  : " << maxVal(lob.spreadHistory)     << "\n";
        std::cout << "Min Spread  : " << minVal(lob.spreadHistory)     << "\n";
        std::cout << "\n--- MICRO-PRICE ANALYTICS ---\n";
        std::cout << "Avg Micro-Price : " << avg(lob.microPriceHistory) << "\n";
        std::cout << "\n--- ORDER BOOK IMBALANCE (OBI) ---\n";
        std::cout << "Avg OBI     : " << avg(lob.obiHistory)           << "\n";
        std::cout << "Max OBI     : " << maxVal(lob.obiHistory)        << "\n";
        std::cout << "Min OBI     : " << minVal(lob.obiHistory)        << "\n";
        std::cout << "========================================\n";

        // Export to CSV for analysis
        exportCSV("lob_metrics.csv");
    }

    void exportCSV(const std::string& filename) {
        std::ofstream f(filename);
        f << "event,spread,micro_price,obi\n";
        size_t n = lob.spreadHistory.size();
        for (size_t i = 0; i < n; ++i) {
            f << i << ","
              << lob.spreadHistory[i]    << ","
              << lob.microPriceHistory[i] << ","
              << lob.obiHistory[i]       << "\n";
        }
        std::cout << "\nMetrics exported to " << filename << "\n";
    }
};

// -----------------------------------------------------------------------
// 6. MAIN
// -----------------------------------------------------------------------

int main(int argc, char* argv[]) {
    int numEvents = 10000;
    if (argc > 1) numEvents = std::atoi(argv[1]);

    std::cout << "Starting LOB Simulation with " << numEvents << " events...\n";
    LOBSimulator sim(100.0);
    sim.run(numEvents);
    return 0;
}