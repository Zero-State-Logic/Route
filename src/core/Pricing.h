#pragma once
#include <string>
#include <string_view>

// Pure pricing math, shared by controllers and unit tests (no framework deps).
namespace route::pricing {

inline double seatTotal(double fare, int qty, double serviceFee) {
    if (qty < 0) qty = 0;
    return fare * static_cast<double>(qty) + serviceFee;
}

// Rentals bill a minimum of one minute; unlock fee applies once.
inline double rentalAmount(int minutes, double perMinRate, double unlockFee) {
    int billable = minutes < 1 ? 1 : minutes;
    return unlockFee + perMinRate * static_cast<double>(billable);
}

// Flat base + per-mode estimate for on-demand rides (shared cabs are cheaper).
inline double rideEstimate(std::string_view mode) {
    double perTrip = (mode == "Cab") ? 5.00 : 8.50;
    return 3.00 + perTrip;
}

}  // namespace route::pricing
