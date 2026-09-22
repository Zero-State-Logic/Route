-- Route: persist pickup/destination (and driver start) coordinates on ride requests.
BEGIN;

ALTER TABLE ride_requests
  ADD COLUMN IF NOT EXISTS origin_lat       DOUBLE PRECISION,
  ADD COLUMN IF NOT EXISTS origin_lng       DOUBLE PRECISION,
  ADD COLUMN IF NOT EXISTS dest_lat         DOUBLE PRECISION,
  ADD COLUMN IF NOT EXISTS dest_lng         DOUBLE PRECISION,
  ADD COLUMN IF NOT EXISTS driver_start_lat DOUBLE PRECISION,
  ADD COLUMN IF NOT EXISTS driver_start_lng DOUBLE PRECISION;

COMMIT;
