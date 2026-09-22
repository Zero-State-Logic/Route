-- Route: persisted payments + auditable wallet ledger.
BEGIN;

CREATE TABLE IF NOT EXISTS payments (
  id           SERIAL PRIMARY KEY,
  user_id      INT REFERENCES users(id),
  amount       NUMERIC(10,2) NOT NULL,
  currency     TEXT NOT NULL DEFAULT 'PKR',
  method       TEXT,
  status       TEXT NOT NULL DEFAULT 'requires_confirmation'
               CHECK (status IN ('requires_confirmation','succeeded','failed')),
  ref          TEXT,
  created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
  confirmed_at TIMESTAMPTZ
);

CREATE TABLE IF NOT EXISTS wallets (
  user_id  INT PRIMARY KEY REFERENCES users(id),
  balance  NUMERIC(12,2) NOT NULL DEFAULT 0,
  currency TEXT NOT NULL DEFAULT 'PKR'
);

-- Append-only ledger: never mutate a prior row.
CREATE TABLE IF NOT EXISTS wallet_ledger (
  id            SERIAL PRIMARY KEY,
  user_id       INT REFERENCES users(id),
  delta         NUMERIC(12,2) NOT NULL,
  reason        TEXT,
  ref           TEXT,
  balance_after NUMERIC(12,2) NOT NULL,
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Give existing users a demo wallet balance.
INSERT INTO wallets (user_id, balance) SELECT id, 5000 FROM users
ON CONFLICT (user_id) DO NOTHING;

COMMIT;
