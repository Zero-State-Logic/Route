-- Route expansion: time-rentals, on-demand rides, trains/bullet trains.
BEGIN;

CREATE TABLE IF NOT EXISTS rental_vehicles (
  id           SERIAL PRIMARY KEY,
  kind         TEXT NOT NULL,
  code         TEXT UNIQUE NOT NULL,
  status       TEXT NOT NULL DEFAULT 'available'
               CHECK (status IN ('available','in_use','maintenance')),
  lat          DOUBLE PRECISION NOT NULL,
  lng          DOUBLE PRECISION NOT NULL,
  unlock_fee   NUMERIC(10,2) NOT NULL,
  per_min_rate NUMERIC(10,2) NOT NULL
);

CREATE TABLE IF NOT EXISTS rentals (
  id         SERIAL PRIMARY KEY,
  user_id    INT REFERENCES users(id),
  vehicle_id INT REFERENCES rental_vehicles(id),
  started_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  ended_at   TIMESTAMPTZ,
  minutes    INT,
  amount     NUMERIC(10,2),
  status     TEXT NOT NULL DEFAULT 'active'
             CHECK (status IN ('active','completed'))
);

CREATE TABLE IF NOT EXISTS ride_requests (
  id            SERIAL PRIMARY KEY,
  user_id       INT REFERENCES users(id),
  origin        TEXT NOT NULL,
  dest          TEXT NOT NULL,
  mode          TEXT NOT NULL,
  status        TEXT NOT NULL DEFAULT 'requested'
                CHECK (status IN ('requested','assigned','enroute','arrived','completed','cancelled')),
  driver_label  TEXT,
  fare_estimate NUMERIC(10,2),
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

INSERT INTO routes (name, origin_id, dest_id, mode) VALUES
  ('Metro Line A: Central -> Riverside', 1, 6, 'Train'),
  ('Bullet North: Central -> Airport T2', 1, 5, 'Bullet train')
ON CONFLICT DO NOTHING;

INSERT INTO trips (route_id, vehicle_label, depart_at, duration_min, fare, seat_count, mode)
SELECT r.id, v.label, now() + v.dep, v.dur, v.fare, 32, r.mode
FROM routes r
JOIN (VALUES
  ('Metro Line A: Central -> Riverside', 'Metro A-14', interval '20 min', 18, 1.80),
  ('Bullet North: Central -> Airport T2', 'Bullet BN-9', interval '35 min', 22, 12.50)
) AS v(rname, label, dep, dur, fare) ON v.rname = r.name
ON CONFLICT DO NOTHING;

DO $$
DECLARE t RECORD; r INT; c INT; lbl TEXT;
BEGIN
  FOR t IN SELECT id FROM trips WHERE id NOT IN (SELECT DISTINCT trip_id FROM trip_seats) LOOP
    FOR r IN 0..7 LOOP
      FOR c IN 1..4 LOOP
        lbl := chr(65 + r) || c::text;
        INSERT INTO trip_seats (trip_id, seat_label, status)
        VALUES (t.id, lbl, 'free') ON CONFLICT DO NOTHING;
      END LOOP;
    END LOOP;
  END LOOP;
END $$;

INSERT INTO rental_vehicles (kind, code, status, lat, lng, unlock_fee, per_min_rate) VALUES
  ('E-scooter','SC-1001','available',40.7128,-74.0060,1.00,0.25),
  ('E-scooter','SC-1002','available',40.7135,-74.0048,1.00,0.25),
  ('E-scooter','SC-1003','available',40.7119,-74.0071,1.00,0.25),
  ('Bike','BK-2001','available',40.7141,-74.0035,0.50,0.15),
  ('Bike','BK-2002','available',40.7108,-74.0082,0.50,0.15),
  ('Bicycle','BC-3001','available',40.7150,-74.0025,0.00,0.08),
  ('Bicycle','BC-3002','available',40.7098,-74.0093,0.00,0.08),
  ('Bicycle','BC-3003','available',40.7160,-74.0015,0.00,0.08)
ON CONFLICT DO NOTHING;

COMMIT;
