-- Route: safety toolkit - pickup PIN, share-trip token, SOS events.
BEGIN;

ALTER TABLE ride_requests
  ADD COLUMN IF NOT EXISTS pin TEXT DEFAULT lpad((floor(random() * 10000))::int::text, 4, '0'),
  ADD COLUMN IF NOT EXISTS share_token TEXT DEFAULT md5(random()::text || clock_timestamp()::text);

CREATE TABLE IF NOT EXISTS sos_events (
  id         SERIAL PRIMARY KEY,
  user_id    INT REFERENCES users(id),
  ride_id    INT REFERENCES ride_requests(id),
  lat        DOUBLE PRECISION,
  lng        DOUBLE PRECISION,
  note       TEXT,
  status     TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open','ack','resolved')),
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

COMMIT;
