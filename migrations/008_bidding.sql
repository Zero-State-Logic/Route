-- Route: inDrive/Bykea-style fare bidding (rider offers, drivers bid).
BEGIN;

CREATE TABLE IF NOT EXISTS ride_offers (
  id            SERIAL PRIMARY KEY,
  user_id       INT REFERENCES users(id),
  mode          TEXT NOT NULL,
  origin        TEXT NOT NULL,
  dest          TEXT NOT NULL,
  origin_lat    DOUBLE PRECISION,
  origin_lng    DOUBLE PRECISION,
  dest_lat      DOUBLE PRECISION,
  dest_lng      DOUBLE PRECISION,
  engine_anchor NUMERIC(10,2),
  offer_fare    NUMERIC(10,2) NOT NULL,
  status        TEXT NOT NULL DEFAULT 'open'
                CHECK (status IN ('open','accepted','cancelled','expired')),
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS ride_bids (
  id         SERIAL PRIMARY KEY,
  offer_id   INT REFERENCES ride_offers(id) ON DELETE CASCADE,
  driver_id  INT REFERENCES drivers(id),
  bid_fare   NUMERIC(10,2) NOT NULL,
  eta_min    INT NOT NULL,
  status     TEXT NOT NULL DEFAULT 'pending'
             CHECK (status IN ('pending','accepted','declined')),
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_ride_bids_offer ON ride_bids(offer_id);

COMMIT;
