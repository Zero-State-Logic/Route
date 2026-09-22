-- Route: real driver fleet + live positions (retires the random mock driver).
BEGIN;

CREATE TABLE IF NOT EXISTS drivers (
  id        SERIAL PRIMARY KEY,
  name      TEXT NOT NULL,
  vehicle   TEXT NOT NULL,
  plate     TEXT UNIQUE NOT NULL,
  rating    NUMERIC(3,2) NOT NULL DEFAULT 4.80,
  status    TEXT NOT NULL DEFAULT 'online'
            CHECK (status IN ('offline','online','on_trip')),
  lat       DOUBLE PRECISION,
  lng       DOUBLE PRECISION,
  last_seen TIMESTAMPTZ
);

ALTER TABLE ride_requests ADD COLUMN IF NOT EXISTS driver_id INT REFERENCES drivers(id);

INSERT INTO drivers (name, vehicle, plate, rating, status, lat, lng, last_seen) VALUES
  ('Omar Khan',    'Suzuki Mehran', 'AEP-431', 4.9, 'online', 24.8610, 67.0200, now()),
  ('Mia Farhan',   'Toyota Prius',  'BFG-112', 4.8, 'online', 24.8550, 67.0300, now()),
  ('Raj Kumar',    'Honda City',    'CSL-908', 4.7, 'online', 24.8700, 67.0100, now()),
  ('Lena Ahmed',   'Suzuki Alto',   'DRT-455', 5.0, 'online', 24.8480, 67.0250, now()),
  ('Bilal Aziz',   'Toyota Corolla','EKM-720', 4.6, 'online', 24.8650, 67.0400, now()),
  ('Sara Malik',   'Suzuki Cultus', 'FPN-331', 4.9, 'online', 24.8420, 67.0150, now())
ON CONFLICT (plate) DO NOTHING;

COMMIT;
