-- Route schema (PostgreSQL). Run once against an empty 'route' database.
BEGIN;

CREATE TABLE IF NOT EXISTS users (
  id            SERIAL PRIMARY KEY,
  email         TEXT UNIQUE NOT NULL,
  password_hash TEXT NOT NULL,
  full_name     TEXT NOT NULL,
  role          TEXT NOT NULL DEFAULT 'rider'
                CHECK (role IN ('rider','driver','manager','admin')),
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS stops (
  id   SERIAL PRIMARY KEY,
  name TEXT NOT NULL,
  code TEXT
);

CREATE TABLE IF NOT EXISTS routes (
  id       SERIAL PRIMARY KEY,
  name     TEXT NOT NULL,
  origin_id INT REFERENCES stops(id),
  dest_id   INT REFERENCES stops(id),
  mode     TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS trips (
  id            SERIAL PRIMARY KEY,
  route_id      INT REFERENCES routes(id),
  vehicle_label TEXT NOT NULL,
  depart_at     TIMESTAMPTZ NOT NULL,
  duration_min  INT NOT NULL,
  fare          NUMERIC(10,2) NOT NULL,
  seat_count    INT NOT NULL DEFAULT 32,
  mode          TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS trip_seats (
  id              SERIAL PRIMARY KEY,
  trip_id         INT REFERENCES trips(id) ON DELETE CASCADE,
  seat_label      TEXT NOT NULL,
  status          TEXT NOT NULL DEFAULT 'free'
                  CHECK (status IN ('free','held','booked')),
  hold_expires_at TIMESTAMPTZ,
  UNIQUE (trip_id, seat_label)
);
CREATE INDEX IF NOT EXISTS idx_trip_seats_trip ON trip_seats(trip_id);

CREATE TABLE IF NOT EXISTS bookings (
  id         SERIAL PRIMARY KEY,
  user_id    INT REFERENCES users(id),
  trip_id    INT REFERENCES trips(id),
  status     TEXT NOT NULL DEFAULT 'confirmed'
             CHECK (status IN ('pending','confirmed','cancelled')),
  total      NUMERIC(10,2) NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS booking_seats (
  id         SERIAL PRIMARY KEY,
  booking_id INT REFERENCES bookings(id) ON DELETE CASCADE,
  seat_label TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS receipts (
  id         SERIAL PRIMARY KEY,
  booking_id INT REFERENCES bookings(id) ON DELETE CASCADE,
  receipt_no TEXT UNIQUE NOT NULL,
  amount     NUMERIC(10,2) NOT NULL,
  issued_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  payload    JSONB NOT NULL
);

COMMIT;
