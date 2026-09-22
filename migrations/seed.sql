-- Demo data for Route. Safe to run after 001_init.sql on a fresh database.
BEGIN;

INSERT INTO stops (name, code) VALUES
  ('Central Station','CEN'), ('Old Town','OLD'), ('Tech Park','TCH'),
  ('Seaside','SEA'), ('Airport T2','APT'), ('Riverside','RIV')
ON CONFLICT DO NOTHING;

INSERT INTO routes (name, origin_id, dest_id, mode) VALUES
  ('Central -> Old Town', 1, 2, 'City bus'),
  ('Central -> Tech Park', 1, 3, 'City bus'),
  ('Central -> Seaside', 1, 4, 'Intercity bus')
ON CONFLICT DO NOTHING;

INSERT INTO trips (route_id, vehicle_label, depart_at, duration_min, fare, seat_count, mode) VALUES
  (1, 'Express 12',   now() + interval '40 min', 45, 2.50, 32, 'City bus'),
  (2, 'Rapid 7',      now() + interval '55 min', 30, 2.10, 32, 'City bus'),
  (3, 'Coastliner 3', now() + interval '85 min', 65, 3.80, 32, 'Intercity bus')
ON CONFLICT DO NOTHING;

-- Generate a 32-seat map (rows A-H, columns 1,2,3,4) for each trip.
DO $$
DECLARE t RECORD; r INT; c INT; lbl TEXT;
BEGIN
  FOR t IN SELECT id FROM trips LOOP
    FOR r IN 0..7 LOOP
      FOR c IN 1..4 LOOP
        lbl := chr(65 + r) || c::text;
        INSERT INTO trip_seats (trip_id, seat_label, status)
        VALUES (t.id, lbl, 'free')
        ON CONFLICT (trip_id, seat_label) DO NOTHING;
      END LOOP;
    END LOOP;
  END LOOP;
END $$;

-- Pre-book some seats so availability looks realistic.
UPDATE trip_seats SET status='booked'
 WHERE trip_id=1 AND seat_label IN ('A1','A2','B3','C4','D1','E2','F3','G4','H1','A3','B1','C2','D3','E4','F1','G2','H3','A4');
UPDATE trip_seats SET status='booked'
 WHERE trip_id=2 AND seat_label IN ('A1','A2','A3','A4','B1','B2','B3','B4','C1','C2','C3','C4','D1','D2','D3','D4','E1','E2','E3','E4','F1','F2','F3','F4','G1','G2','G3');

COMMIT;
