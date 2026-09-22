-- Route improvements: unify currency to PKR + ride-completion fields.
BEGIN;

-- 1) Currency: put transit fares and rental rates on a PKR scale.
UPDATE trips SET fare = CASE mode
  WHEN 'City bus'      THEN 120
  WHEN 'Intercity bus' THEN 350
  WHEN 'Train'         THEN 90
  WHEN 'Bullet train'  THEN 2500
  ELSE fare END;

UPDATE rental_vehicles SET
  unlock_fee = CASE kind
    WHEN 'E-scooter' THEN 30 WHEN 'Bike' THEN 20 WHEN 'Bicycle' THEN 0 ELSE unlock_fee END,
  per_min_rate = CASE kind
    WHEN 'E-scooter' THEN 8  WHEN 'Bike' THEN 5  WHEN 'Bicycle' THEN 3 ELSE per_min_rate END;

-- 2) Ride completion: final settled fare, rider rating, tip.
ALTER TABLE ride_requests
  ADD COLUMN IF NOT EXISTS fare_final NUMERIC(10,2),
  ADD COLUMN IF NOT EXISTS rating     INT,
  ADD COLUMN IF NOT EXISTS tip        NUMERIC(10,2);

COMMIT;
