#pragma once
#include <cmath>
#include <string>

// Route fare engine: a fare is built from the real road trip, not a flat number.
//   total = round( (base + perKm*km + perMin*min + fuelLitres*fuelPrice + bookingFee) * surge , step )
// with a per-mode minimum. Fuel is an explicit line: litres = km * (litresPer100km/100).
namespace route::fare {

struct FuelCfg { double pricePerLitre; double litresPer100km; };
struct FareCfg {
    std::string currency; std::string symbol;
    double baseFare, perKm, perMin, bookingFee, minFare, roundingStep;
    FuelCfg fuel;
};

struct Breakdown {
    std::string currency, symbol;
    double distanceKm, durationMin;
    double base, distanceCost, timeCost, fuelLitres, fuelCost, bookingFee;
    double subtotal, surgeMult, surgeAmount, total;
    bool onRoad;
};

// Per-mode rate cards (PKR). Fuel price ~ Pakistan 2026 petrol; consumption per mode.
inline FareCfg configFor(const std::string &mode) {
    FareCfg c;
    c.currency = "PKR"; c.symbol = "Rs";
    c.bookingFee = 20; c.roundingStep = 5;
    if (mode == "Cab") {            // shared, cheaper, lighter cars
        c.baseFare = 80;  c.perKm = 14; c.perMin = 1.5; c.minFare = 120;
        c.fuel = { 290.0, 7.0 };
    } else if (mode == "Bike" || mode == "E-scooter" || mode == "Bicycle") {
        c.baseFare = 40;  c.perKm = 9;  c.perMin = 1.0; c.minFare = 60;
        c.fuel = { 290.0, 2.5 };
    } else {                         // Taxi (default)
        c.baseFare = 100; c.perKm = 16; c.perMin = 2.0; c.minFare = 150;
        c.fuel = { 290.0, 9.0 };
    }
    return c;
}

inline double roundTo(double v, double step) {
    return step > 0 ? std::round(v / step) * step : std::round(v);
}

inline Breakdown compute(const std::string &mode, double distanceMeters,
                         double durationSeconds, double surge, bool onRoad) {
    FareCfg c = configFor(mode);
    Breakdown b{};
    b.currency = c.currency; b.symbol = c.symbol; b.onRoad = onRoad;
    b.distanceKm = distanceMeters / 1000.0;
    b.durationMin = durationSeconds / 60.0;
    b.base = c.baseFare;
    b.distanceCost = c.perKm * b.distanceKm;
    b.timeCost = c.perMin * b.durationMin;
    b.fuelLitres = b.distanceKm * (c.fuel.litresPer100km / 100.0);
    b.fuelCost = b.fuelLitres * c.fuel.pricePerLitre;
    b.bookingFee = c.bookingFee;
    b.surgeMult = surge < 1.0 ? 1.0 : surge;
    b.subtotal = b.base + b.distanceCost + b.timeCost + b.fuelCost + b.bookingFee;
    double surged = b.subtotal * b.surgeMult;
    if (surged < c.minFare) surged = c.minFare;
    b.total = roundTo(surged, c.roundingStep);
    b.surgeAmount = roundTo(surged, c.roundingStep) - roundTo(b.subtotal, c.roundingStep);
    if (b.surgeAmount < 0) b.surgeAmount = 0;
    return b;
}

// Great-circle metres between two lat/lng points (fallback when routing is down).
inline double haversineMeters(double aLat, double aLng, double bLat, double bLng) {
    const double PI = 3.14159265358979323846;
    const double R = 6371000.0, r = PI / 180.0;
    double dLat = (bLat - aLat) * r, dLng = (bLng - aLng) * r;
    double s = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(aLat * r) * std::cos(bLat * r) * std::sin(dLng / 2) * std::sin(dLng / 2);
    return 2 * R * std::atan2(std::sqrt(s), std::sqrt(1 - s));
}

}  // namespace route::fare
