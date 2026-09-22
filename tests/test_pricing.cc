#include <gtest/gtest.h>

#include "core/Pricing.h"

using namespace route::pricing;

TEST(Pricing, SeatTotalAddsServiceFee) {
    EXPECT_DOUBLE_EQ(seatTotal(2.50, 2, 0.40), 5.40);
}

TEST(Pricing, SeatTotalZeroQtyIsJustFee) {
    EXPECT_DOUBLE_EQ(seatTotal(2.50, 0, 0.40), 0.40);
}

TEST(Pricing, SeatTotalClampsNegativeQty) {
    EXPECT_DOUBLE_EQ(seatTotal(2.50, -3, 0.40), 0.40);
}

TEST(Pricing, RentalBillsMinimumOneMinute) {
    EXPECT_DOUBLE_EQ(rentalAmount(0, 0.25, 1.00), 1.25);
}

TEST(Pricing, RentalTenMinutes) {
    EXPECT_DOUBLE_EQ(rentalAmount(10, 0.25, 1.00), 3.50);
}

TEST(Pricing, RentalBicycleNoUnlockFee) {
    EXPECT_DOUBLE_EQ(rentalAmount(5, 0.08, 0.00), 0.40);
}

TEST(Pricing, RideTaxiCostsMoreThanCab) {
    EXPECT_GT(rideEstimate("Taxi"), rideEstimate("Cab"));
}
