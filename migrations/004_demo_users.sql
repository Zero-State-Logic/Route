-- Route demo accounts (known passwords for local dev/testing).
-- Passwords: rider@route.org / Ride12345   |   manager@route.org / Manage12345
BEGIN;
INSERT INTO users (email, password_hash, full_name, role) VALUES
  ('rider@route.org', 'pbkdf2$120000$jCvvDvfK6lbYRW82yu3Jtw==$uV1eKiaUUZsb/eDk6rX2rJTX51AMBjGvpush+0/ztrw=', 'Demo Rider', 'rider'),
  ('manager@route.org', 'pbkdf2$120000$U7PsfZ1q5U965UzFQFYjHQ==$qla5qG7AXAKPwM7i0fPUBN8/Rwau6zrK0Y2d9CBumxg=', 'Demo Manager', 'manager')
ON CONFLICT (email) DO UPDATE SET password_hash=EXCLUDED.password_hash, role=EXCLUDED.role, full_name=EXCLUDED.full_name;
COMMIT;
