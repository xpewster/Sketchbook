#pragma once

#include "../http.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <string>
#include <iostream>
#include <algorithm>
#include "log.hpp"


using json = nlohmann::json;

struct TrainData {
    std::string headsign0; // Direction 0, e.g. southbound
    std::string headsign1; // Direction 1, e.g. northbound
    float minsToNextTrain0 = 999;
    float minsToNextTrain1 = 999;
    bool available0 = false;
    bool available1 = false;
};

class TrainMonitor {
public:
    TrainMonitor(const std::string& apiBase, const std::string& apiKey, 
                 const std::string& stopId0, const std::string& stopId1)
        : apiBase_(apiBase), apiKey_(apiKey), stopId0_(stopId0), stopId1_(stopId1) {
        lastUpdateTime_ = std::chrono::steady_clock::now() - std::chrono::hours(1); // Force initial update
        LOG_INFO << "TrainMonitor initialized with API base: " << apiBase_
                  << ", stopId0: " << stopId0_ << ", stopId1: " << stopId1_ << "\n";
    }
    
    TrainData getTrain() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdateTime_).count();
        
        // Update every minute for train arrivals
        if (elapsed >= 60) {
            fetchTrain(0);
            fetchTrain(1);
            lastUpdateTime_ = now;
        }
        
        return cachedTrain_;
    }

private:
    void fetchTrain(int stopIndex) {
        // Build URL - OneBusAway format
        auto stopId = (stopIndex == 0) ? stopId0_ : stopId1_;
        std::string path = "/api/where/arrivals-and-departures-for-stop/" + stopId + 
                          ".json?key=" + apiKey_ +
                          "&minutesBefore=10";
        
        HttpResponse response = get(apiBase_ + path);
        
        if (response.isOk()) {
            // LOG_DEBUG << "Train API response [" << response.statusCode << "] received for stop " << stopIndex << ": " << response.body.substr(0, 50) << "...\n";
            parseTrainData(response.body, stopIndex);
            // LOG_DEBUG << "Train data updated for stop " << stopIndex 
            //           << ": [Headsign " << (stopIndex == 0 ? cachedTrain_.headsign0 : cachedTrain_.headsign1)
            //           << "] [Minutes " << (stopIndex == 0 ? cachedTrain_.minsToNextTrain0 : cachedTrain_.minsToNextTrain1) << "]\n";
            if (stopIndex == 0) {
                cachedTrain_.available0 = true;
            } else {
                cachedTrain_.available1 = true;
            }
        } else {
            LOG_WARN << "Train API request failed for stop " << stopIndex 
                      << ": " << response.statusCode << "\n";
            if (stopIndex == 0) {
                cachedTrain_.available0 = false;
            } else {
                cachedTrain_.available1 = false;
            }
        }
    }
    
    void parseTrainData(const std::string& jsonStr, int stopIndex) {
        try {
            json data = json::parse(jsonStr);
            
            // Extract current time
            int64_t currentTime = 0;
            if (data.contains("currentTime")) {
                currentTime = data["currentTime"].get<int64_t>();
            }
            
            // Navigate to arrivals array
            if (!data.contains("data") || !data["data"].contains("entry") || 
                !data["data"]["entry"].contains("arrivalsAndDepartures")) {
                LOG_WARN << "Unexpected JSON structure for stop " << stopIndex << "\n";
                return;
            }
            
            auto arrivals = data["data"]["entry"]["arrivalsAndDepartures"];
            
            // Find the next train (soonest arrival with mins >= 0)
            float minMinutes = 999;
            std::string nextHeadsign = "No data";
            
            for (const auto& arrival : arrivals) {
                if (!arrival.value("predicted", false)) {
                    continue; // Skip non-predicted (scheduled) arrivals
                }

                // Get predicted time
                int64_t predictedTime = arrival["predictedArrivalTime"].get<int64_t>();
                int64_t arrivalTime = (predictedTime > 0) ? predictedTime : 0;
                
                // Calculate minutes
                float mins = (arrivalTime - currentTime) / 60000.0f;
                
                // Only consider upcoming trains (>= 0 minutes)
                if (mins >= 0 && mins < minMinutes) {
                    minMinutes = mins;            
                }

                // Get headsign
                if (arrival.contains("tripHeadsign")) {
                    nextHeadsign = arrival["tripHeadsign"].get<std::string>();
                    
                    // Shorten common destinations
                    // if (nextHeadsign.find("Federal Way Downtown") != std::string::npos) {
                    //     nextHeadsign = "Federal Way";
                    // } else if (nextHeadsign.find("Lynnwood City Center") != std::string::npos) {
                    //     nextHeadsign = "Lynnwood";
                    // }
                }
            }
            
            // Update cached data for this stop
            if (stopIndex == 0) {
                cachedTrain_.headsign0 = nextHeadsign;
                cachedTrain_.minsToNextTrain0 = minMinutes;
            } else {
                cachedTrain_.headsign1 = nextHeadsign;
                cachedTrain_.minsToNextTrain1 = minMinutes;
            }
            
        } catch (const json::exception& e) {
            LOG_ERROR << "JSON parsing error for stop " << stopIndex << ": " << e.what() << "\n";
        }
    }

    std::string apiBase_;
    std::string apiKey_;
    std::string stopId0_;
    std::string stopId1_;
    TrainData cachedTrain_;
    std::chrono::steady_clock::time_point lastUpdateTime_;
};
